variable "name_prefix" {
  description = "Prefix applied to all GameLift resource names"
  type        = string
}

variable "environment" {
  description = "Deployment environment: dev | staging | prod"
  type        = string
  validation {
    condition     = contains(["dev", "staging", "prod"], var.environment)
    error_message = "environment must be dev, staging, or prod."
  }
}

variable "server_zip_path" {
  description = "Local path to the packaged UE5 Linux server zip. Leave empty to skip S3 upload."
  type        = string
  default     = ""
}

variable "instance_type" {
  description = "EC2 instance type for the GameLift fleet"
  type        = string
  default     = "c5.large"
}

variable "max_sessions_per_instance" {
  description = "Number of concurrent game sessions per fleet instance"
  type        = number
  default     = 4
}

variable "max_players_per_session" {
  description = "Maximum players per game session"
  type        = number
  default     = 8
}
