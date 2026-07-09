##############################################################
# terraform/security.tf
# Production hardening:
#   - AWS WAF v2 on CloudFront (rate limiting + common rule sets)
#   - VPC Flow Logs → CloudWatch
#   - CloudTrail API audit trail → S3 + CloudWatch
#   - Secrets Manager auto-rotation for DB password
#   - S3 replication for asset backup
##############################################################

# ─── WAF v2 (must be us-east-1 for CloudFront) ─────────────────
resource "aws_wafv2_web_acl" "main" {
  provider    = aws.us_east_1
  name        = "${local.name_prefix}-waf"
  description = "RealmForge CloudFront WAF — rate limiting + OWASP top-10 protection"
  scope       = "CLOUDFRONT"

  default_action { allow {} }

  # ── Rule 1: AWS Managed Common Rule Set ────────────────────
  rule {
    name     = "AWSManagedRulesCommonRuleSet"
    priority = 1

    override_action { none {} }

    statement {
      managed_rule_group_statement {
        name        = "AWSManagedRulesCommonRuleSet"
        vendor_name = "AWS"

        # Exclude rules that block WebSocket upgrade headers
        rule_action_override {
          name          = "SizeRestrictions_BODY"
          action_to_use { allow {} }
        }
      }
    }

    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name                = "${local.name_prefix}-common-rules"
      sampled_requests_enabled   = true
    }
  }

  # ── Rule 2: Known Bad Inputs ────────────────────────────────
  rule {
    name     = "AWSManagedRulesKnownBadInputsRuleSet"
    priority = 2

    override_action { none {} }

    statement {
      managed_rule_group_statement {
        name        = "AWSManagedRulesKnownBadInputsRuleSet"
        vendor_name = "AWS"
      }
    }

    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name                = "${local.name_prefix}-bad-inputs"
      sampled_requests_enabled   = true
    }
  }

  # ── Rule 3: IP Rate Limit (2000 req/5min per IP) ───────────
  rule {
    name     = "IPRateLimit"
    priority = 3

    action { block {} }

    statement {
      rate_based_statement {
        limit              = 2000
        aggregate_key_type = "IP"
      }
    }

    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name                = "${local.name_prefix}-rate-limit"
      sampled_requests_enabled   = true
    }
  }

  # ── Rule 4: API-specific stricter rate limit ────────────────
  rule {
    name     = "APIRateLimit"
    priority = 4

    action { block {} }

    statement {
      rate_based_statement {
        limit              = 500
        aggregate_key_type = "IP"

        scope_down_statement {
          byte_match_statement {
            search_string         = "/api/"
            positional_constraint = "STARTS_WITH"
            field_to_match { uri_path {} }
            text_transformation {
              priority = 0
              type     = "LOWERCASE"
            }
          }
        }
      }
    }

    visibility_config {
      cloudwatch_metrics_enabled = true
      metric_name                = "${local.name_prefix}-api-rate-limit"
      sampled_requests_enabled   = true
    }
  }

  visibility_config {
    cloudwatch_metrics_enabled = true
    metric_name                = "${local.name_prefix}-waf"
    sampled_requests_enabled   = true
  }

  tags = { Name = "${local.name_prefix}-waf" }
}

# Associate WAF with CloudFront distribution
resource "aws_wafv2_web_acl_association" "cloudfront" {
  provider     = aws.us_east_1
  resource_arn = module.cdn.cloudfront_arn
  web_acl_arn  = aws_wafv2_web_acl.main.arn
}

# ─── VPC Flow Logs ─────────────────────────────────────────────
resource "aws_cloudwatch_log_group" "vpc_flow_logs" {
  name              = "/aws/vpc/${local.name_prefix}-flow-logs"
  retention_in_days = 30
}

resource "aws_iam_role" "vpc_flow_logs" {
  name = "${local.name_prefix}-vpc-flow-logs-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "vpc-flow-logs.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy" "vpc_flow_logs" {
  role = aws_iam_role.vpc_flow_logs.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Action = [
        "logs:CreateLogGroup", "logs:CreateLogStream",
        "logs:PutLogEvents", "logs:DescribeLogGroups", "logs:DescribeLogStreams"
      ]
      Resource = "*"
    }]
  })
}

resource "aws_flow_log" "main" {
  log_destination      = aws_cloudwatch_log_group.vpc_flow_logs.arn
  log_destination_type = "cloud-watch-logs"
  traffic_type         = "ALL"
  vpc_id               = module.vpc.vpc_id
  iam_role_arn         = aws_iam_role.vpc_flow_logs.arn

  tags = { Name = "${local.name_prefix}-flow-log" }
}

# ─── CloudTrail ────────────────────────────────────────────────
resource "aws_s3_bucket" "cloudtrail" {
  bucket        = "${local.name_prefix}-cloudtrail-${local.suffix}"
  force_destroy = false
}

resource "aws_s3_bucket_public_access_block" "cloudtrail" {
  bucket                  = aws_s3_bucket.cloudtrail.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_s3_bucket_policy" "cloudtrail" {
  bucket = aws_s3_bucket.cloudtrail.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Sid    = "AWSCloudTrailAclCheck"
        Effect = "Allow"
        Principal = { Service = "cloudtrail.amazonaws.com" }
        Action   = "s3:GetBucketAcl"
        Resource = aws_s3_bucket.cloudtrail.arn
      },
      {
        Sid    = "AWSCloudTrailWrite"
        Effect = "Allow"
        Principal = { Service = "cloudtrail.amazonaws.com" }
        Action   = "s3:PutObject"
        Resource = "${aws_s3_bucket.cloudtrail.arn}/AWSLogs/*"
        Condition = {
          StringEquals = { "s3:x-amz-acl" = "bucket-owner-full-control" }
        }
      }
    ]
  })
}

resource "aws_cloudwatch_log_group" "cloudtrail" {
  name              = "/aws/cloudtrail/${local.name_prefix}"
  retention_in_days = 90
}

resource "aws_cloudtrail" "main" {
  name                          = "${local.name_prefix}-trail"
  s3_bucket_name                = aws_s3_bucket.cloudtrail.id
  cloud_watch_logs_group_arn    = "${aws_cloudwatch_log_group.cloudtrail.arn}:*"
  cloud_watch_logs_role_arn     = aws_iam_role.vpc_flow_logs.arn
  include_global_service_events = true
  is_multi_region_trail         = true
  enable_log_file_validation    = true

  event_selector {
    read_write_type           = "All"
    include_management_events = true

    data_resource {
      type   = "AWS::S3::Object"
      values = ["${module.cdn.assets_bucket_arn}/"]
    }
  }

  tags = { Name = "${local.name_prefix}-cloudtrail" }
}

# ─── Secrets Manager Auto-Rotation ─────────────────────────────
# Rotates the DB password every 30 days via a Lambda function
resource "aws_secretsmanager_secret_rotation" "db_password" {
  secret_id           = "${local.name_prefix}/db-password"
  rotation_lambda_arn = aws_lambda_function.secret_rotator.arn

  rotation_rules {
    automatically_after_days = 30
  }

  depends_on = [aws_lambda_function.secret_rotator]
}

resource "aws_iam_role" "secret_rotator" {
  name = "${local.name_prefix}-secret-rotator-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy_attachment" "secret_rotator_basic" {
  role       = aws_iam_role.secret_rotator.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

resource "aws_iam_role_policy" "secret_rotator" {
  role = aws_iam_role.secret_rotator.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = ["secretsmanager:DescribeSecret", "secretsmanager:GetSecretValue",
                  "secretsmanager:PutSecretValue", "secretsmanager:UpdateSecretVersionStage"]
        Resource = "*"
      },
      {
        Effect = "Allow"
        Action = ["ec2:CreateNetworkInterface", "ec2:DeleteNetworkInterface",
                  "ec2:DescribeNetworkInterfaces"]
        Resource = "*"
      }
    ]
  })
}

# Inline Lambda for PostgreSQL password rotation
data "archive_file" "rotator" {
  type        = "zip"
  output_path = "/tmp/rotator.zip"

  source {
    content  = <<-PYTHON
import boto3, psycopg2, secrets, string

def handler(event, context):
    sm = boto3.client('secretsmanager')
    secret_id = event['SecretId']
    token      = event['ClientRequestToken']
    step       = event['Step']

    meta = sm.describe_secret(SecretId=secret_id)
    current = sm.get_secret_value(SecretId=secret_id, VersionStage='AWSCURRENT')
    import json
    creds = json.loads(current['SecretString'])

    if step == 'createSecret':
        new_pass = ''.join(secrets.choice(string.ascii_letters + string.digits) for _ in range(32))
        new_creds = {**creds, 'password': new_pass}
        sm.put_secret_value(SecretId=secret_id, ClientRequestToken=token,
                            SecretString=json.dumps(new_creds), VersionStages=['AWSPENDING'])

    elif step == 'setSecret':
        pending = sm.get_secret_value(SecretId=secret_id, VersionStage='AWSPENDING')
        new_creds = json.loads(pending['SecretString'])
        conn = psycopg2.connect(host=creds['host'], port=creds.get('port',5432),
                                dbname=creds['dbname'], user=creds['username'],
                                password=creds['password'])
        conn.autocommit = True
        with conn.cursor() as cur:
            cur.execute("ALTER USER %s WITH PASSWORD %s",
                        (new_creds['username'], new_creds['password']))
        conn.close()

    elif step == 'testSecret':
        pending = sm.get_secret_value(SecretId=secret_id, VersionStage='AWSPENDING')
        new_creds = json.loads(pending['SecretString'])
        conn = psycopg2.connect(host=new_creds['host'], port=new_creds.get('port',5432),
                                dbname=new_creds['dbname'], user=new_creds['username'],
                                password=new_creds['password'])
        conn.close()

    elif step == 'finishSecret':
        sm.update_secret_version_stage(SecretId=secret_id, VersionStage='AWSCURRENT',
                                       MoveToVersionId=token,
                                       RemoveFromVersionId=meta['VersionIdsToStages'].get('AWSCURRENT',[None])[0])
PYTHON
    filename = "index.py"
  }
}

resource "aws_lambda_function" "secret_rotator" {
  filename         = data.archive_file.rotator.output_path
  function_name    = "${local.name_prefix}-secret-rotator"
  role             = aws_iam_role.secret_rotator.arn
  handler          = "index.handler"
  runtime          = "python3.12"
  timeout          = 30

  vpc_config {
    subnet_ids         = module.vpc.private_subnet_ids
    security_group_ids = [module.vpc.sg_ecs_id]
  }

  environment {
    variables = {
      SECRETS_MANAGER_ENDPOINT = "https://secretsmanager.${var.aws_region}.amazonaws.com"
    }
  }
}

resource "aws_lambda_permission" "secret_rotator" {
  function_name = aws_lambda_function.secret_rotator.function_name
  statement_id  = "AllowSecretsManagerInvoke"
  action        = "lambda:InvokeFunction"
  principal     = "secretsmanager.amazonaws.com"
}

# ─── S3 Cross-Region Replication for Asset Backup ─────────────
resource "aws_s3_bucket" "assets_replica" {
  provider      = aws.eu_west_1
  bucket        = "${local.name_prefix}-assets-replica-${local.suffix}"
  force_destroy = false
}

resource "aws_s3_bucket_versioning" "assets_replica" {
  provider = aws.eu_west_1
  bucket   = aws_s3_bucket.assets_replica.id
  versioning_configuration { status = "Enabled" }
}

resource "aws_s3_bucket_public_access_block" "assets_replica" {
  provider                = aws.eu_west_1
  bucket                  = aws_s3_bucket.assets_replica.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

resource "aws_iam_role" "s3_replication" {
  name = "${local.name_prefix}-s3-replication-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "s3.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy" "s3_replication" {
  role = aws_iam_role.s3_replication.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["s3:GetReplicationConfiguration", "s3:ListBucket"]
        Resource = module.cdn.assets_bucket_arn
      },
      {
        Effect   = "Allow"
        Action   = ["s3:GetObjectVersionForReplication", "s3:GetObjectVersionAcl",
                    "s3:GetObjectVersionTagging"]
        Resource = "${module.cdn.assets_bucket_arn}/*"
      },
      {
        Effect   = "Allow"
        Action   = ["s3:ReplicateObject", "s3:ReplicateDelete", "s3:ReplicateTags"]
        Resource = "${aws_s3_bucket.assets_replica.arn}/*"
      }
    ]
  })
}

# ─── S3 Replication Configuration ─────────────────────────────
resource "aws_s3_bucket_replication_configuration" "assets" {
  role   = aws_iam_role.s3_replication.arn
  bucket = module.cdn.assets_bucket_name

  rule {
    id     = "ReplicateAll"
    status = "Enabled"

    destination {
      bucket        = aws_s3_bucket.assets_replica.arn
      storage_class = "STANDARD_IA"  # cheaper for rarely-accessed backups
    }

    delete_marker_replication { status = "Enabled" }
  }

  # Source bucket must have versioning enabled (done in cdn/main.tf)
  depends_on = [aws_s3_bucket_versioning.assets_replica]
}
