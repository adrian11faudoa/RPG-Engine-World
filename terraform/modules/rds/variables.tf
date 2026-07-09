variable "name_prefix" {
  description = "Prefix applied to all RDS resource names"
  type        = string
}

variable "vpc_id" {
  description = "VPC ID where RDS will be deployed"
  type        = string
}

variable "private_subnet_ids" {
  description = "List of private subnet IDs for the RDS subnet group (needs 2+ for Multi-AZ)"
  type        = list(string)
}

variable "db_password" {
  description = "RDS master password — must be at least 16 characters"
  type        = string
  sensitive   = true
}

variable "environment" {
  description = "Deployment environment: dev | staging | prod"
  type        = string
  validation {
    condition     = contains(["dev", "staging", "prod"], var.environment)
    error_message = "environment must be dev, staging, or prod."
  }
}

variable "sg_rds_id" {
  description = "Security group ID to attach to the RDS instance"
  type        = string
  default     = ""
}

variable "instance_class" {
  description = "RDS instance class. Overrides the environment-based default."
  type        = string
  default     = ""
}

variable "allocated_storage_gb" {
  description = "Initial allocated storage in GB"
  type        = number
  default     = 20
}

variable "backup_retention_days" {
  description = "Number of days to retain automated backups. 0 disables backups."
  type        = number
  default     = 7
}
