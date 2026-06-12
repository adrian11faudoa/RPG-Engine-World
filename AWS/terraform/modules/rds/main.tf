##############################################################
# modules/rds/main.tf
# PostgreSQL on RDS for campaigns, users, sessions
##############################################################

resource "aws_db_subnet_group" "main" {
  name       = "${var.name_prefix}-db-subnet-group"
  subnet_ids = var.private_subnet_ids
}

resource "aws_db_parameter_group" "postgres" {
  name   = "${var.name_prefix}-pg15"
  family = "postgres15"

  parameter {
    name  = "log_connections"
    value = "1"
  }
  parameter {
    name  = "log_min_duration_statement"
    value = "1000"  # log queries >1s
  }
  parameter {
    name  = "shared_preload_libraries"
    value = "pg_stat_statements"
  }
}

resource "aws_db_instance" "main" {
  identifier     = "${var.name_prefix}-postgres"
  engine         = "postgres"
  engine_version = "15.4"

  instance_class = var.environment == "prod" ? "db.t3.medium" : "db.t3.micro"
  allocated_storage     = 20
  max_allocated_storage = 100
  storage_type          = "gp3"
  storage_encrypted     = true

  db_name  = "realmforge"
  username = "realmforge"
  password = var.db_password

  db_subnet_group_name   = aws_db_subnet_group.main.name
  vpc_security_group_ids = [var.sg_rds_id]
  parameter_group_name   = aws_db_parameter_group.postgres.name

  backup_retention_period = var.environment == "prod" ? 7 : 1
  backup_window           = "03:00-04:00"
  maintenance_window      = "Sun:04:00-Sun:05:00"

  deletion_protection      = var.environment == "prod"
  skip_final_snapshot      = var.environment != "prod"
  final_snapshot_identifier = var.environment == "prod" ? "${var.name_prefix}-final-snapshot" : null

  performance_insights_enabled = true

  enabled_cloudwatch_logs_exports = ["postgresql"]

  tags = { Name = "${var.name_prefix}-rds" }
}

variable "name_prefix"        {}
variable "vpc_id"             {}
variable "private_subnet_ids" { type = list(string) }
variable "db_password"        { sensitive = true }
variable "environment"        {}
variable "sg_rds_id"          { default = "" }

output "endpoint" { value = aws_db_instance.main.endpoint }
output "db_name"  { value = aws_db_instance.main.db_name }
