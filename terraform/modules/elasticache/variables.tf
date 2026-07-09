variable "name_prefix" {
  description = "Prefix applied to all ElastiCache resource names"
  type        = string
}

variable "vpc_id" {
  description = "VPC ID where ElastiCache will be deployed"
  type        = string
}

variable "private_subnet_ids" {
  description = "List of private subnet IDs for the ElastiCache subnet group"
  type        = list(string)
}

variable "sg_redis_id" {
  description = "Security group ID to attach to the Redis cluster"
  type        = string
  default     = ""
}

variable "node_type" {
  description = "ElastiCache node type. cache.t3.micro is cheapest; use cache.r6g.large for production load."
  type        = string
  default     = "cache.t3.micro"
}

variable "num_cache_nodes" {
  description = "Number of cache nodes. 1 = no HA. 2+ = primary + replicas."
  type        = number
  default     = 1
  validation {
    condition     = var.num_cache_nodes >= 1
    error_message = "num_cache_nodes must be at least 1."
  }
}

variable "snapshot_retention_days" {
  description = "Number of days to retain Redis snapshots. 0 disables snapshots."
  type        = number
  default     = 1
}
