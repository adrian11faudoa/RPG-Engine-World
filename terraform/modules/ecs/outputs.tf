output "ecr_url" {
  description = "ECR repository URL — push dashboard images here"
  value       = aws_ecr_repository.dashboard.repository_url
}

output "alb_dns_name" {
  description = "ALB DNS name — used as CloudFront origin and for Route53 alias"
  value       = aws_lb.main.dns_name
}

output "alb_arn_suffix" {
  description = "ALB ARN suffix (app/name/id) — required for CloudWatch metric dimensions"
  value       = aws_lb.main.arn_suffix
}

output "alb_arn" {
  description = "Full ALB ARN — used for WAF association and attribute modifications"
  value       = aws_lb.main.arn
}

output "cluster_name" {
  description = "ECS cluster name — used for CLI commands and auto-scaling resource IDs"
  value       = aws_ecs_cluster.main.name
}

output "service_name" {
  description = "ECS service name — used for force-new-deployment and auto-scaling"
  value       = aws_ecs_service.dashboard.name
}

output "task_exec_role_arn" {
  description = "ECS task execution role ARN — allows ECS to pull images and write logs"
  value       = aws_iam_role.ecs_task_execution.arn
}

output "task_role_arn" {
  description = "ECS task role ARN — grants the running container access to S3, GameLift, etc."
  value       = aws_iam_role.ecs_task.arn
}

output "alb_https_listener_arn" {
  description = "HTTPS listener ARN — used by CodeDeploy blue/green to route traffic"
  value       = aws_lb_listener.https.arn
}

output "blue_target_group_name" {
  description = "Blue target group name for CodeDeploy blue/green deployments"
  value       = aws_lb_target_group.dashboard.name
}

output "green_target_group_name" {
  description = "Green target group name — CodeDeploy swaps traffic here during deployment"
  value       = "${var.name_prefix}-tg-dashboard-green"
}

output "task_role_name" {
  description = "ECS task role name — use for attaching inline IAM policies"
  value       = aws_iam_role.ecs_task.name
}
