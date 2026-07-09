# terraform/envs/dev/backend.tf
terraform {
  backend "s3" {
    bucket         = "realmforge-terraform-state"
    key            = "dev/terraform.tfstate"
    region         = "us-east-1"
    encrypt        = true
    dynamodb_table = "realmforge-tf-lock"
  }
}
