variable "name_prefix"     {}
variable "environment"     {}
variable "server_zip_path" { default = "" }

output "fleet_id"           { value = aws_gamelift_fleet.main.id }
output "alias_id"           { value = aws_gamelift_alias.main.id }
output "queue_name"         { value = aws_gamelift_game_session_queue.main.name }
output "matchmaking_config" { value = aws_gamelift_matchmaking_configuration.main.name }
output "build_bucket"       { value = aws_s3_bucket.build_artifacts.bucket }
