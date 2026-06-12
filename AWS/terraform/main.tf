##############################################################
# RealmForge Engine — AWS Infrastructure (Terraform)
# Provisions: VPC, ECS (dashboard), GameLift (game server),
#             RDS (campaigns), ElastiCache (sessions),
#             S3+CloudFront (assets/CDN), ALB, ACM, Route53
##############################################################

terraform {
  required_version = ">= 1.6.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.30"
    }
    random = { source = "hashicorp/random" }
  }

  # Remote state — swap bucket name after first apply
  backend "s3" {
    bucket         = "realmforge-terraform-state"
    key            = "prod/terraform.tfstate"
    region         = "us-east-1"
    encrypt        = true
    dynamodb_table = "realmforge-tf-lock"
  }
}

provider "aws" {
  region = var.aws_region

  default_tags {
    tags = {
      Project     = "RealmForge"
      Environment = var.environment
      ManagedBy   = "Terraform"
    }
  }
}

# ─── Shared random suffix (keeps names unique) ─────────────────
resource "random_id" "suffix" {
  byte_length = 4
}

locals {
  name_prefix = "realmforge-${var.environment}"
  suffix      = random_id.suffix.hex
}

# ─── Modules ───────────────────────────────────────────────────

module "vpc" {
  source      = "./modules/vpc"
  name_prefix = local.name_prefix
  environment = var.environment
}

module "rds" {
  source            = "./modules/rds"
  name_prefix       = local.name_prefix
  vpc_id            = module.vpc.vpc_id
  private_subnet_ids = module.vpc.private_subnet_ids
  db_password       = var.db_password
  environment       = var.environment
}

module "elasticache" {
  source            = "./modules/elasticache"
  name_prefix       = local.name_prefix
  vpc_id            = module.vpc.vpc_id
  private_subnet_ids = module.vpc.private_subnet_ids
}

module "ecs" {
  source              = "./modules/ecs"
  name_prefix         = local.name_prefix
  vpc_id              = module.vpc.vpc_id
  public_subnet_ids   = module.vpc.public_subnet_ids
  private_subnet_ids  = module.vpc.private_subnet_ids
  dashboard_image     = var.dashboard_ecr_image
  rds_endpoint        = module.rds.endpoint
  redis_endpoint      = module.elasticache.endpoint
  certificate_arn     = module.cdn.certificate_arn
  environment         = var.environment
  db_password         = var.db_password
  jwt_secret          = var.jwt_secret
}

module "gamelift" {
  source          = "./modules/gamelift"
  name_prefix     = local.name_prefix
  server_zip_path = var.server_build_zip
  environment     = var.environment
}

module "cdn" {
  source          = "./modules/cdn"
  name_prefix     = local.name_prefix
  domain_name     = var.domain_name
  alb_dns_name    = module.ecs.alb_dns_name
  environment     = var.environment
}
