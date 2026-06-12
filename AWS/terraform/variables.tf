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
