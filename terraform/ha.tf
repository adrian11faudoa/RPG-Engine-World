##############################################################
# terraform/ha.tf
# High-Availability additions:
#   - RDS Multi-AZ + read replica
#   - ElastiCache Redis cluster mode
#   - ECS Blue/Green deployment via CodeDeploy
#   - GameLift second fleet in eu-west-1 (multi-region failover)
##############################################################

# ─── RDS Multi-AZ (upgrade existing instance) ──────────────────
# Enable by setting var.rds_multi_az = true in tfvars
resource "aws_db_instance" "read_replica" {
  count = var.enable_rds_read_replica ? 1 : 0

  identifier             = "${local.name_prefix}-postgres-replica"
  replicate_source_db    = module.rds.db_identifier
  instance_class         = var.environment == "prod" ? "db.t3.medium" : "db.t3.micro"
  publicly_accessible    = false
  vpc_security_group_ids = [module.vpc.sg_rds_id]
  skip_final_snapshot    = true

  # Read replicas inherit most settings from primary
  tags = { Name = "${local.name_prefix}-rds-replica", Role = "read-replica" }
}

output "rds_replica_endpoint" {
  description = "RDS read replica endpoint — route read-heavy dashboard queries here"
  value       = var.enable_rds_read_replica ? aws_db_instance.read_replica[0].endpoint : null
}

# ─── ElastiCache Cluster Mode (multi-shard for scale) ──────────
# Separate from the base single-node cluster in elasticache module.
# Enable with var.enable_redis_cluster = true for prod.
resource "aws_elasticache_replication_group" "cluster" {
  count = var.enable_redis_cluster ? 1 : 0

  replication_group_id = "${local.name_prefix}-redis-cluster"
  description          = "RealmForge Redis cluster mode — sharded for horizontal scale"

  node_type               = "cache.r6g.large"
  port                    = 6379
  parameter_group_name    = "default.redis7.cluster.on"
  subnet_group_name       = "${local.name_prefix}-redis-subnet"  # reuse from elasticache module
  security_group_ids      = [module.vpc.sg_redis_id]
  engine_version          = "7.0"

  # Cluster mode: 3 shards × 1 replica = 6 nodes total
  num_node_groups         = 3
  replicas_per_node_group = 1

  at_rest_encryption_enabled = true
  transit_encryption_enabled = true
  automatic_failover_enabled = true
  multi_az_enabled           = true

  snapshot_retention_limit = 3
  snapshot_window          = "05:00-06:00"

  tags = { Name = "${local.name_prefix}-redis-cluster" }
}

output "redis_cluster_endpoint" {
  description = "Redis cluster-mode configuration endpoint (use for cluster-aware clients)"
  value       = var.enable_redis_cluster ? aws_elasticache_replication_group.cluster[0].configuration_endpoint_address : null
}

# ─── ECS Blue/Green Deployment via CodeDeploy ──────────────────
resource "aws_codedeploy_app" "dashboard" {
  count            = var.enable_blue_green ? 1 : 0
  name             = "${local.name_prefix}-dashboard"
  compute_platform = "ECS"
}

resource "aws_codedeploy_deployment_group" "dashboard" {
  count                  = var.enable_blue_green ? 1 : 0
  app_name               = aws_codedeploy_app.dashboard[0].name
  deployment_group_name  = "${local.name_prefix}-dashboard-dg"
  service_role_arn       = aws_iam_role.codedeploy[0].arn
  deployment_config_name = "CodeDeployDefault.ECSLinear10PercentEvery1Minutes"

  ecs_service {
    cluster_name = module.ecs.cluster_name
    service_name = module.ecs.service_name
  }

  deployment_style {
    deployment_option = "WITH_TRAFFIC_CONTROL"
    deployment_type   = "BLUE_GREEN"
  }

  blue_green_deployment_config {
    deployment_ready_option {
      action_on_timeout = "CONTINUE_DEPLOYMENT"
    }

    terminate_blue_instances_on_deployment_success {
      action                           = "TERMINATE"
      termination_wait_time_in_minutes = 5
    }
  }

  load_balancer_info {
    target_group_pair_info {
      prod_traffic_route { listener_arns = [module.ecs.alb_https_listener_arn] }

      target_group { name = module.ecs.blue_target_group_name }
      target_group { name = module.ecs.green_target_group_name }
    }
  }

  auto_rollback_configuration {
    enabled = true
    events  = ["DEPLOYMENT_FAILURE", "DEPLOYMENT_STOP_ON_ALARM"]
  }

  alarm_configuration {
    alarms  = ["${local.name_prefix}-ecs-cpu-high"]
    enabled = true
  }
}

resource "aws_iam_role" "codedeploy" {
  count = var.enable_blue_green ? 1 : 0
  name  = "${local.name_prefix}-codedeploy-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "codedeploy.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy_attachment" "codedeploy_ecs" {
  count      = var.enable_blue_green ? 1 : 0
  role       = aws_iam_role.codedeploy[0].name
  policy_arn = "arn:aws:iam::aws:policy/AWSCodeDeployRoleForECS"
}

# ─── GameLift Multi-Region (EU-West-1 failover fleet) ──────────
provider "aws" {
  alias  = "eu_west_1"
  region = "eu-west-1"
}

resource "aws_gamelift_fleet" "eu_failover" {
  count    = var.enable_gamelift_multiregion ? 1 : 0
  provider = aws.eu_west_1

  name        = "${local.name_prefix}-fleet-eu"
  build_id    = module.gamelift.fleet_id  # Same build, different region
  description = "RealmForge EU failover fleet"

  ec2_instance_type = "c5.large"

  ec2_inbound_permission {
    from_port = 7777
    to_port   = 7780
    ip_range  = "0.0.0.0/0"
    protocol  = "UDP"
  }

  runtime_configuration {
    server_process {
      concurrent_executions = 4
      launch_path           = "/local/game/RealmForgeServer.sh"
      parameters            = "-log -port=7777"
    }
  }

  new_game_session_protection_policy = "NoProtection"
}

resource "aws_gamelift_alias" "eu_failover" {
  count    = var.enable_gamelift_multiregion ? 1 : 0
  provider = aws.eu_west_1

  name        = "${local.name_prefix}-alias-eu"
  description = "RealmForge EU fleet alias"

  routing_strategy {
    type     = "SIMPLE"
    fleet_id = aws_gamelift_fleet.eu_failover[0].id
  }
}

# Multi-region queue routes players to lowest-latency region
resource "aws_gamelift_game_session_queue" "multiregion" {
  count = var.enable_gamelift_multiregion ? 1 : 0
  name  = "${local.name_prefix}-queue-global"

  # US first, EU fallback
  destinations {
    destination_arn = module.gamelift.alias_arn
  }

  destinations {
    destination_arn = aws_gamelift_alias.eu_failover[0].arn
  }

  player_latency_policy {
    maximum_individual_player_latency_milliseconds = 150
    policy_duration_seconds                        = 30
  }

  player_latency_policy {
    maximum_individual_player_latency_milliseconds = 300
  }

  timeout_in_seconds = 60
}

# ─── HA Feature Toggle Variables ───────────────────────────────
# (Add these to variables.tf)
