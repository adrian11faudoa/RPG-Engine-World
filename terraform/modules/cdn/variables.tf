variable "name_prefix" {
  description = "Prefix applied to all CDN resource names"
  type        = string
}

variable "domain_name" {
  description = "Primary public domain (e.g. realmforge.gg). A wildcard cert covers *.domain_name too."
  type        = string
  validation {
    condition     = can(regex("^[a-z0-9][a-z0-9.-]+\\.[a-z]{2,}$", var.domain_name))
    error_message = "domain_name must be a valid domain like realmforge.gg"
  }
}

variable "alb_dns_name" {
  description = "DNS name of the ALB that CloudFront proxies /api/* and /* to"
  type        = string
}

variable "environment" {
  description = "Deployment environment: dev | staging | prod"
  type        = string
  default     = "prod"
}

variable "price_class" {
  description = "CloudFront price class. PriceClass_100 = NA+EU only (cheapest)."
  type        = string
  default     = "PriceClass_100"
  validation {
    condition     = contains(["PriceClass_100","PriceClass_200","PriceClass_All"], var.price_class)
    error_message = "price_class must be PriceClass_100, PriceClass_200, or PriceClass_All."
  }
}

variable "assets_max_ttl" {
  description = "Maximum CloudFront cache TTL (seconds) for /assets/* objects. Default 1 year."
  type        = number
  default     = 31536000
}
