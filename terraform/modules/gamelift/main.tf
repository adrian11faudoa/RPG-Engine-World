##############################################################
# modules/gamelift/main.tf
# Hosts the Unreal Engine dedicated server on AWS GameLift
# with auto-scaling, matchmaking, and multi-region queuing
##############################################################

# ─── Server Build ──────────────────────────────────────────────
resource "aws_gamelift_build" "server" {
  name             = "${var.name_prefix}-server-build"
  operating_system = "AMAZON_LINUX_2"

  storage_location {
    bucket   = aws_s3_bucket.build_artifacts.bucket
    key      = "server-builds/RealmForgeServer-Linux.zip"
    role_arn = aws_iam_role.gamelift_build.arn
  }

  depends_on = [aws_s3_object.server_build]
}

# ─── S3 for build artifacts ────────────────────────────────────
resource "aws_s3_bucket" "build_artifacts" {
  bucket        = "${var.name_prefix}-gamelift-builds"
  force_destroy = true
}

resource "aws_s3_bucket_versioning" "build_artifacts" {
  bucket = aws_s3_bucket.build_artifacts.id
  versioning_configuration { status = "Enabled" }
}

resource "aws_s3_object" "server_build" {
  bucket = aws_s3_bucket.build_artifacts.bucket
  key    = "server-builds/RealmForgeServer-Linux.zip"
  source = var.server_zip_path

  lifecycle {
    ignore_changes = [source]  # Updated via CI/CD, not Terraform
  }
}

# ─── IAM ───────────────────────────────────────────────────────
resource "aws_iam_role" "gamelift_build" {
  name = "${var.name_prefix}-gamelift-build-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "gamelift.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy" "gamelift_build_s3" {
  role = aws_iam_role.gamelift_build.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect   = "Allow"
      Action   = ["s3:GetObject", "s3:GetObjectVersion"]
      Resource = "${aws_s3_bucket.build_artifacts.arn}/*"
    }]
  })
}

# ─── Fleet ─────────────────────────────────────────────────────
resource "aws_gamelift_fleet" "main" {
  name        = "${var.name_prefix}-fleet"
  build_id    = aws_gamelift_build.server.id
  description = "RealmForge dedicated game server fleet"

  # c5.large: 2 vCPU / 4GB — good for 8-player sessions
  ec2_instance_type = var.instance_type

  # Number of concurrent game sessions per instance
  # c5.large handles ~4 sessions comfortably (8 players each)
  ec2_inbound_permission {
    from_port = 7777
    to_port   = 7780
    ip_range  = "0.0.0.0/0"
    protocol  = "UDP"
  }

  ec2_inbound_permission {
    from_port = 7777
    to_port   = 7780
    ip_range  = "0.0.0.0/0"
    protocol  = "TCP"
  }

  runtime_configuration {
    # Launch RealmForge server process
    server_process {
      concurrent_executions = var.max_sessions_per_instance
      launch_path           = "/local/game/RealmForgeServer.sh"
      parameters            = "-log -port=7777"
    }
  }

  # Allow GameLift to terminate old instances during scaling
  new_game_session_protection_policy = "NoProtection"

  resource_creation_limit_policy {
    new_game_sessions_per_creator = 5
    policy_period_in_minutes      = 15
  }
}

# ─── Fleet Auto-Scaling ────────────────────────────────────────
resource "aws_gamelift_fleet_autoscaling" "scale_up" {
  fleet_id       = aws_gamelift_fleet.main.id
  name           = "${var.name_prefix}-scale-up"
  policy_type    = "TargetBased"

  target_configuration {
    target_value = 70  # % active game sessions target
  }
}

# ─── Alias (stable endpoint, can failover to another fleet) ────
resource "aws_gamelift_alias" "main" {
  name        = "${var.name_prefix}-alias"
  description = "RealmForge primary fleet alias"

  routing_strategy {
    type     = "SIMPLE"
    fleet_id = aws_gamelift_fleet.main.id
  }
}

# ─── Game Session Queue ────────────────────────────────────────
# Sessions go through the queue → routes to least-loaded fleet
resource "aws_gamelift_game_session_queue" "main" {
  name = "${var.name_prefix}-queue"

  destinations {
    destination_arn = aws_gamelift_alias.main.arn
  }

  player_latency_policy {
    maximum_individual_player_latency_milliseconds = 200
    policy_duration_seconds                        = 30
  }

  player_latency_policy {
    maximum_individual_player_latency_milliseconds = 400
    # No duration = this is the final fallback
  }

  timeout_in_seconds = 60
}

# ─── Matchmaking Configuration ─────────────────────────────────
resource "aws_gamelift_matchmaking_rule_set" "main" {
  name = "${var.name_prefix}-rules"

  rule_set_body = jsonencode({
    name        = "RealmForgeRules"
    ruleLanguageVersion = "1.0"
    playerAttributes = [
      {
        name       = "skill"
        type       = "number"
        default    = 1000
      }
    ]
    teams = [
      {
        name      = "adventurers"
        maxPlayers = 8
        minPlayers = 1
      }
    ]
    rules = [
      {
        name       = "LowLatencyFirst"
        type       = "latency"
        maxLatency = 300
      }
    ]
    expansions = [
      {
        target    = "rules[LowLatencyFirst].maxLatency"
        steps = [
          { waitTimeSeconds = 15, value = 500 },
          { waitTimeSeconds = 30, value = 1000 }
        ]
      }
    ]
  })
}

resource "aws_gamelift_matchmaking_configuration" "main" {
  name                        = "${var.name_prefix}-matchmaking"
  acceptance_required         = false
  request_timeout_seconds     = 60
  rule_set_name               = aws_gamelift_matchmaking_rule_set.main.name
  game_session_queue_arns     = [aws_gamelift_game_session_queue.main.arn]
  additional_player_count     = 0

  game_property {
    key   = "GameMode"
    value = "Standard"
  }
}
