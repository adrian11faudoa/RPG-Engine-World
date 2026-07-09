output "fleet_id" {
  description = "GameLift fleet ID"
  value       = aws_gamelift_fleet.main.id
}

output "alias_id" {
  description = "GameLift alias ID (stable endpoint over fleet)"
  value       = aws_gamelift_alias.main.id
}

output "alias_arn" {
  description = "GameLift alias ARN"
  value       = aws_gamelift_alias.main.arn
}

output "queue_name" {
  description = "Game session queue name"
  value       = aws_gamelift_game_session_queue.main.name
}

output "matchmaking_config" {
  description = "FlexMatch matchmaking configuration name"
  value       = aws_gamelift_matchmaking_configuration.main.name
}

output "build_bucket" {
  description = "S3 bucket holding GameLift server build artifacts"
  value       = aws_s3_bucket.build_artifacts.bucket
}

output "build_bucket_arn" {
  description = "ARN of the GameLift build artifact bucket"
  value       = aws_s3_bucket.build_artifacts.arn
}
