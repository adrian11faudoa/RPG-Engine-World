##############################################################
# modules/elasticache/main.tf
# Redis for: active game sessions, matchmaking state,
#            player presence, campaign locks
##############################################################

resource "aws_elasticache_subnet_group" "main" {
  name       = "${var.name_prefix}-redis-subnet"
  subnet_ids = var.private_subnet_ids
}

resource "aws_elasticache_parameter_group" "redis7" {
  name   = "${var.name_prefix}-redis7"
  family = "redis7"

  parameter {
    name  = "maxmemory-policy"
    value = "allkeys-lru"
  }
}

resource "aws_elasticache_replication_group" "main" {
  replication_group_id = "${var.name_prefix}-redis"
  description          = "RealmForge session cache"

  node_type            = var.node_type
  port                 = 6379
  parameter_group_name = aws_elasticache_parameter_group.redis7.name
  subnet_group_name    = aws_elasticache_subnet_group.main.name
  security_group_ids   = [var.sg_redis_id]

  engine_version       = "7.0"
  num_cache_clusters   = var.num_cache_nodes

  at_rest_encryption_enabled  = true
  transit_encryption_enabled  = false  # set true with TLS in app

  snapshot_retention_limit = var.snapshot_retention_days
  snapshot_window          = "05:00-06:00"

  auto_minor_version_upgrade = true

  tags = { Name = "${var.name_prefix}-redis" }
}

variable "name_prefix"        {}
variable "vpc_id"             {}
variable "private_subnet_ids" { type = list(string) }
variable "sg_redis_id"        { default = "" }

output "endpoint" {
  value = aws_elasticache_replication_group.main.primary_endpoint_address
}
