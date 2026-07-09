# terraform/envs/prod/backend.tf
# Override the root backend for the prod environment.
# Run: terraform init -backend-config=envs/prod/backend.tf

terraform {
  backend "s3" {
    bucket         = "realmforge-terraform-state"
    key            = "prod/terraform.tfstate"
    region         = "us-east-1"
    encrypt        = true
    dynamodb_table = "realmforge-tf-lock"
  }
}
