variable "name_prefix" {
  description = "Prefix applied to all ECS resource names"
  type        = string
}

variable "vpc_id" {
  description = "VPC ID where ECS tasks will run"
  type        = string
}

variable "public_subnet_ids" {
  description = "Public subnet IDs for the Application Load Balancer"
  type        = list(string)
}

variable "private_subnet_ids" {
  description = "Private subnet IDs for ECS Fargate tasks"
  type        = list(string)
}

variable "dashboard_image" {
  description = "Full ECR image URI for the dashboard container (e.g. 123456789.dkr.ecr.us-east-1.amazonaws.com/repo:tag)"
  type        = string
}

variable "rds_endpoint" {
  description = "RDS instance endpoint (host:port) — injected as DATABASE_URL into container"
  type        = string
}

variable "redis_endpoint" {
  description = "ElastiCache Redis primary endpoint address — injected as REDIS_URL into container"
  type        = string
}

variable "certificate_arn" {
  description = "ACM certificate ARN to attach to the ALB HTTPS listener"
  type        = string
}

variable "environment" {
  description = "Deployment environment: dev | staging | prod — controls task count and deletion protection"
  type        = string
  validation {
    condition     = contains(["dev", "staging", "prod"], var.environment)
    error_message = "environment must be dev, staging, or prod."
  }
}

variable "db_password" {
  description = "Database password — stored in Secrets Manager, never in env vars directly"
  type        = string
  sensitive   = true
}

variable "jwt_secret" {
  description = "JWT signing secret — stored in Secrets Manager"
  type        = string
  sensitive   = true
}

variable "sg_alb_id" {
  description = "Security group ID to attach to the Application Load Balancer"
  type        = string
  default     = ""
}

variable "sg_ecs_id" {
  description = "Security group ID to attach to ECS tasks"
  type        = string
  default     = ""
}

variable "task_cpu" {
  description = "Fargate task CPU units (256=0.25vCPU, 512=0.5vCPU, 1024=1vCPU)"
  type        = number
  default     = 512
}

variable "task_memory" {
  description = "Fargate task memory in MiB"
  type        = number
  default     = 1024
}

variable "min_tasks" {
  description = "Minimum number of running ECS tasks (auto-scaling lower bound)"
  type        = number
  default     = 1
}

variable "max_tasks" {
  description = "Maximum number of running ECS tasks (auto-scaling upper bound)"
  type        = number
  default     = 10
}

variable "domain_name" {
  description = "Public domain name — injected as DOMAIN env var into the container"
  type        = string
  default     = ""
}
