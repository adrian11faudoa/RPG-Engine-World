##############################################################
# variables.tf
##############################################################

variable "aws_region" {
  default     = "us-east-1"
  description = "AWS region for all resources"
}

variable "environment" {
  default     = "prod"
  description = "Environment tag (dev | staging | prod)"
}

variable "domain_name" {
  description = "Your domain, e.g. realmforge.gg"
}

variable "db_password" {
  description = "RDS master password"
  sensitive   = true
}

variable "jwt_secret" {
  description = "JWT signing secret for dashboard auth"
  sensitive   = true
}

variable "dashboard_ecr_image" {
  description = "ECR image URI for the dashboard container, e.g. 123456789.dkr.ecr.us-east-1.amazonaws.com/realmforge-dashboard:latest"
}

variable "server_build_zip" {
  description = "Local path to the GameLift server build zip"
  default     = "../server/build/RealmForgeServer-Linux.zip"
}

variable "alert_email" {
  description = "Email address for CloudWatch alarm notifications"
  default     = ""
}

# ─── HA / Multi-region toggles (false = cheaper dev setup) ─────
variable "enable_rds_read_replica" {
  description = "Deploy an RDS read replica for read-heavy dashboard queries"
  type        = bool
  default     = false
}

variable "enable_redis_cluster" {
  description = "Deploy Redis in cluster mode (3 shards × 1 replica). More expensive but horizontally scalable."
  type        = bool
  default     = false
}

variable "enable_blue_green" {
  description = "Use CodeDeploy blue/green deployments for ECS instead of rolling updates"
  type        = bool
  default     = false
}

variable "enable_gamelift_multiregion" {
  description = "Deploy a GameLift failover fleet in eu-west-1 for lower-latency EU sessions"
  type        = bool
  default     = false
}
