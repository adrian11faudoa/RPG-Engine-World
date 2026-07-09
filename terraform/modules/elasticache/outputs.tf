output "endpoint" {
  description = "ElastiCache Redis primary endpoint address"
  value       = aws_elasticache_replication_group.main.primary_endpoint_address
}

output "port" {
  description = "Redis port (always 6379)"
  value       = 6379
}

output "redis_url" {
  description = "Full Redis connection URL for use in application environment variables"
  value       = "redis://${aws_elasticache_replication_group.main.primary_endpoint_address}:6379"
}

output "replication_group_id" {
  description = "ElastiCache replication group ID"
  value       = aws_elasticache_replication_group.main.id
}

output "arn" {
  description = "ARN of the ElastiCache replication group"
  value       = aws_elasticache_replication_group.main.arn
}
