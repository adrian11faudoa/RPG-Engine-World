##############################################################
# terraform/queues.tf
# SQS queues for async processing:
#   - Dead-letter queue for failed WebSocket events
#   - Roll log archival queue (batch → S3 Parquet)
#   - Session cleanup queue (GameLift session ended → DB update)
##############################################################

# ─── Dead Letter Queue ─────────────────────────────────────────
resource "aws_sqs_queue" "ws_events_dlq" {
  name                       = "${local.name_prefix}-ws-events-dlq"
  message_retention_seconds  = 1209600  # 14 days
  visibility_timeout_seconds = 30

  tags = { Name = "${local.name_prefix}-ws-dlq" }
}

# ─── WebSocket Events Queue ────────────────────────────────────
resource "aws_sqs_queue" "ws_events" {
  name                       = "${local.name_prefix}-ws-events"
  visibility_timeout_seconds = 30
  message_retention_seconds  = 86400  # 1 day

  redrive_policy = jsonencode({
    deadLetterTargetArn = aws_sqs_queue.ws_events_dlq.arn
    maxReceiveCount     = 3  # move to DLQ after 3 failed attempts
  })

  tags = { Name = "${local.name_prefix}-ws-events" }
}

# ─── Session Cleanup Queue ─────────────────────────────────────
resource "aws_sqs_queue" "session_cleanup_dlq" {
  name                      = "${local.name_prefix}-session-cleanup-dlq"
  message_retention_seconds = 1209600
}

resource "aws_sqs_queue" "session_cleanup" {
  name                       = "${local.name_prefix}-session-cleanup"
  visibility_timeout_seconds = 60
  message_retention_seconds  = 3600  # 1 hour

  redrive_policy = jsonencode({
    deadLetterTargetArn = aws_sqs_queue.session_cleanup_dlq.arn
    maxReceiveCount     = 3
  })

  tags = { Name = "${local.name_prefix}-session-cleanup" }
}

# ─── Roll Log Archive Queue ────────────────────────────────────
resource "aws_sqs_queue" "roll_archive_dlq" {
  name                      = "${local.name_prefix}-roll-archive-dlq"
  message_retention_seconds = 1209600
}

resource "aws_sqs_queue" "roll_archive" {
  name                       = "${local.name_prefix}-roll-archive"
  visibility_timeout_seconds = 120
  message_retention_seconds  = 86400

  redrive_policy = jsonencode({
    deadLetterTargetArn = aws_sqs_queue.roll_archive_dlq.arn
    maxReceiveCount     = 5
  })

  tags = { Name = "${local.name_prefix}-roll-archive" }
}

# ─── IAM: ECS task gets SQS access ────────────────────────────
resource "aws_iam_role_policy" "ecs_sqs" {
  role = module.ecs.task_role_name

  name = "${local.name_prefix}-ecs-sqs-policy"

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect = "Allow"
        Action = [
          "sqs:SendMessage",
          "sqs:ReceiveMessage",
          "sqs:DeleteMessage",
          "sqs:GetQueueAttributes",
          "sqs:ChangeMessageVisibility",
        ]
        Resource = [
          aws_sqs_queue.ws_events.arn,
          aws_sqs_queue.session_cleanup.arn,
          aws_sqs_queue.roll_archive.arn,
        ]
      }
    ]
  })
}

# ─── Lambda: Session Cleanup Processor ─────────────────────────
data "archive_file" "session_cleanup" {
  type        = "zip"
  output_path = "/tmp/session_cleanup.zip"

  source {
    filename = "index.mjs"
    content  = <<-JS
import { RDSDataClient, ExecuteStatementCommand } from '@aws-sdk/client-rds-data';

const rds = new RDSDataClient({ region: process.env.AWS_REGION });

export const handler = async (event) => {
  for (const record of event.Records) {
    const body = JSON.parse(record.body);
    const { gameSessionId, endedAt } = body;

    console.log(`[Cleanup] Ending session: ${gameSessionId}`);

    await rds.send(new ExecuteStatementCommand({
      resourceArn: process.env.RDS_ARN,
      secretArn:   process.env.DB_SECRET_ARN,
      database:    'realmforge',
      sql: `UPDATE game_sessions
            SET status = 'ENDED', ended_at = :endedAt
            WHERE gamelift_session_id = :sessionId`,
      parameters: [
        { name: 'endedAt',   value: { stringValue: endedAt || new Date().toISOString() } },
        { name: 'sessionId', value: { stringValue: gameSessionId } },
      ],
    }));
  }
};
JS
  }
}

resource "aws_lambda_function" "session_cleanup" {
  filename         = data.archive_file.session_cleanup.output_path
  function_name    = "${local.name_prefix}-session-cleanup"
  role             = aws_iam_role.session_cleanup_lambda.arn
  handler          = "index.handler"
  runtime          = "nodejs20.x"
  timeout          = 60

  environment {
    variables = {
      AWS_REGION    = var.aws_region
      RDS_ARN       = module.rds.db_arn
      DB_SECRET_ARN = "${local.name_prefix}/db-password"
    }
  }
}

resource "aws_lambda_event_source_mapping" "session_cleanup" {
  event_source_arn = aws_sqs_queue.session_cleanup.arn
  function_name    = aws_lambda_function.session_cleanup.arn
  batch_size       = 10
}

resource "aws_iam_role" "session_cleanup_lambda" {
  name = "${local.name_prefix}-session-cleanup-lambda-role"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Action    = "sts:AssumeRole"
      Effect    = "Allow"
      Principal = { Service = "lambda.amazonaws.com" }
    }]
  })
}

resource "aws_iam_role_policy_attachment" "session_cleanup_basic" {
  role       = aws_iam_role.session_cleanup_lambda.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole"
}

resource "aws_iam_role_policy" "session_cleanup_lambda" {
  role = aws_iam_role.session_cleanup_lambda.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = ["sqs:ReceiveMessage", "sqs:DeleteMessage", "sqs:GetQueueAttributes"]
        Resource = aws_sqs_queue.session_cleanup.arn
      },
      {
        Effect   = "Allow"
        Action   = ["rds-data:ExecuteStatement"]
        Resource = module.rds.db_arn
      },
      {
        Effect   = "Allow"
        Action   = ["secretsmanager:GetSecretValue"]
        Resource = "*"
      }
    ]
  })
}

# ─── CloudWatch Alarms on DLQs ─────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "ws_dlq_depth" {
  alarm_name          = "${local.name_prefix}-ws-dlq-messages"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 1
  metric_name         = "ApproximateNumberOfMessagesVisible"
  namespace           = "AWS/SQS"
  period              = 300
  statistic           = "Sum"
  threshold           = 0
  alarm_description   = "WS events DLQ has messages — WebSocket event processing failed"
  alarm_actions       = [aws_sns_topic.alerts.arn]
  treat_missing_data  = "notBreaching"

  dimensions = { QueueName = aws_sqs_queue.ws_events_dlq.name }
}

resource "aws_cloudwatch_metric_alarm" "session_dlq_depth" {
  alarm_name          = "${local.name_prefix}-session-dlq-messages"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 1
  metric_name         = "ApproximateNumberOfMessagesVisible"
  namespace           = "AWS/SQS"
  period              = 300
  statistic           = "Sum"
  threshold           = 0
  alarm_description   = "Session cleanup DLQ has messages — sessions may not be ending in DB"
  alarm_actions       = [aws_sns_topic.alerts.arn]
  treat_missing_data  = "notBreaching"

  dimensions = { QueueName = aws_sqs_queue.session_cleanup_dlq.name }
}

# ─── Outputs ───────────────────────────────────────────────────
output "ws_events_queue_url"     { value = aws_sqs_queue.ws_events.url }
output "session_cleanup_queue_url" { value = aws_sqs_queue.session_cleanup.url }
output "roll_archive_queue_url"  { value = aws_sqs_queue.roll_archive.url }
