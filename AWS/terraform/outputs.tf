##############################################################
# outputs.tf
##############################################################

output "dashboard_url" {
  description = "Public URL of the RealmForge dashboard"
  value       = "https://${var.domain_name}"
}

output "game_server_fleet_id" {
  description = "GameLift fleet ID for players to connect to"
  value       = module.gamelift.fleet_id
}

output "rds_endpoint" {
  description = "RDS PostgreSQL endpoint (private)"
  value       = module.rds.endpoint
  sensitive   = true
}

output "redis_endpoint" {
  description = "ElastiCache Redis endpoint (private)"
  value       = module.elasticache.endpoint
  sensitive   = true
}

output "ecr_repository_url" {
  description = "ECR URL to push dashboard Docker image to"
  value       = module.ecs.ecr_url
}

output "alb_dns_name" {
  description = "ALB DNS name (alias this in Route53)"
  value       = module.ecs.alb_dns_name
}

output "cloudfront_domain" {
  description = "CloudFront distribution domain"
  value       = module.cdn.cloudfront_domain
}

output "assets_bucket" {
  description = "S3 bucket for map/asset uploads"
  value       = module.cdn.assets_bucket_name
}
