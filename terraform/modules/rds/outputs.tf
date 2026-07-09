output "endpoint" {
  description = "RDS instance endpoint (host:port)"
  value       = aws_db_instance.main.endpoint
}

output "host" {
  description = "RDS instance hostname (without port)"
  value       = aws_db_instance.main.address
}

output "port" {
  description = "RDS instance port"
  value       = aws_db_instance.main.port
}

output "db_name" {
  description = "Name of the application database"
  value       = aws_db_instance.main.db_name
}

output "db_username" {
  description = "RDS master username"
  value       = aws_db_instance.main.username
  sensitive   = true
}

output "db_arn" {
  description = "ARN of the RDS instance"
  value       = aws_db_instance.main.arn
}

output "db_identifier" {
  description = "RDS instance identifier (used for snapshots, monitoring)"
  value       = aws_db_instance.main.identifier
}
