variable "name_prefix"        {}
variable "vpc_id"             {}
variable "public_subnet_ids"  { type = list(string) }
variable "private_subnet_ids" { type = list(string) }
variable "dashboard_image"    {}
variable "rds_endpoint"       {}
variable "redis_endpoint"     {}
variable "certificate_arn"    {}
variable "environment"        {}
variable "db_password"        { sensitive = true }
variable "jwt_secret"         { sensitive = true }
variable "sg_alb_id"          { default = "" }
variable "sg_ecs_id"          { default = "" }

output "ecr_url"      { value = aws_ecr_repository.dashboard.repository_url }
output "alb_dns_name" { value = aws_lb.main.dns_name }
output "cluster_name" { value = aws_ecs_cluster.main.name }
output "service_name" { value = aws_ecs_service.dashboard.name }
