##############################################################
# monitoring.tf  (add to terraform root or as module)
# CloudWatch alarms, dashboards, SNS notifications
##############################################################

# ─── SNS Topic for Alerts ──────────────────────────────────────
resource "aws_sns_topic" "alerts" {
  name = "${local.name_prefix}-alerts"
}

resource "aws_sns_topic_subscription" "email" {
  topic_arn = aws_sns_topic.alerts.arn
  protocol  = "email"
  endpoint  = var.alert_email   # add to variables.tf
}

# ─── ECS CPU Alarm ─────────────────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "ecs_cpu_high" {
  alarm_name          = "${local.name_prefix}-ecs-cpu-high"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 2
  metric_name         = "CPUUtilization"
  namespace           = "AWS/ECS"
  period              = 60
  statistic           = "Average"
  threshold           = 85
  alarm_description   = "ECS CPU > 85% for 2 minutes"
  alarm_actions       = [aws_sns_topic.alerts.arn]

  dimensions = {
    ClusterName = "${local.name_prefix}-cluster"
    ServiceName = "${local.name_prefix}-dashboard-svc"
  }
}

# ─── RDS CPU Alarm ─────────────────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "rds_cpu" {
  alarm_name          = "${local.name_prefix}-rds-cpu"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 3
  metric_name         = "CPUUtilization"
  namespace           = "AWS/RDS"
  period              = 60
  statistic           = "Average"
  threshold           = 80
  alarm_actions       = [aws_sns_topic.alerts.arn]

  dimensions = {
    DBInstanceIdentifier = "${local.name_prefix}-postgres"
  }
}

# ─── RDS Free Storage Alarm ────────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "rds_storage" {
  alarm_name          = "${local.name_prefix}-rds-storage-low"
  comparison_operator = "LessThanThreshold"
  evaluation_periods  = 1
  metric_name         = "FreeStorageSpace"
  namespace           = "AWS/RDS"
  period              = 300
  statistic           = "Average"
  threshold           = 2147483648   # 2 GB in bytes
  alarm_description   = "RDS free storage < 2GB"
  alarm_actions       = [aws_sns_topic.alerts.arn]

  dimensions = {
    DBInstanceIdentifier = "${local.name_prefix}-postgres"
  }
}

# ─── ALB 5xx Error Rate ────────────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "alb_5xx" {
  alarm_name          = "${local.name_prefix}-alb-5xx"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 2
  metric_name         = "HTTPCode_ELB_5XX_Count"
  namespace           = "AWS/ApplicationELB"
  period              = 60
  statistic           = "Sum"
  threshold           = 20
  alarm_description   = "More than 20 ALB 5xx errors per minute"
  alarm_actions       = [aws_sns_topic.alerts.arn]
  treat_missing_data  = "notBreaching"
}

# ─── GameLift Active Sessions ──────────────────────────────────
resource "aws_cloudwatch_metric_alarm" "gamelift_sessions_high" {
  alarm_name          = "${local.name_prefix}-gamelift-sessions-full"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 3
  metric_name         = "ActiveGameSessions"
  namespace           = "AWS/GameLift"
  period              = 60
  statistic           = "Average"
  threshold           = 35   # alert before hitting hard cap
  alarm_description   = "GameLift active sessions > 35 — consider scaling fleet"
  alarm_actions       = [aws_sns_topic.alerts.arn]

  dimensions = {
    FleetId = module.gamelift.fleet_id
  }
}

# ─── CloudWatch Dashboard ──────────────────────────────────────
resource "aws_cloudwatch_dashboard" "main" {
  dashboard_name = "${local.name_prefix}-ops"

  dashboard_body = jsonencode({
    widgets = [
      {
        type = "metric"
        properties = {
          title  = "ECS CPU & Memory"
          period = 60
          metrics = [
            ["AWS/ECS", "CPUUtilization",    "ClusterName", "${local.name_prefix}-cluster", "ServiceName", "${local.name_prefix}-dashboard-svc"],
            ["AWS/ECS", "MemoryUtilization", "ClusterName", "${local.name_prefix}-cluster", "ServiceName", "${local.name_prefix}-dashboard-svc"]
          ]
          view = "timeSeries"
          stat = "Average"
        }
        x = 0; y = 0; width = 12; height = 6
      },
      {
        type = "metric"
        properties = {
          title  = "GameLift Active Sessions"
          period = 60
          metrics = [
            ["AWS/GameLift", "ActiveGameSessions",       "FleetId", module.gamelift.fleet_id],
            ["AWS/GameLift", "CurrentPlayerSessions",    "FleetId", module.gamelift.fleet_id],
            ["AWS/GameLift", "AvailableGameSessions",    "FleetId", module.gamelift.fleet_id],
          ]
          stat = "Average"
        }
        x = 12; y = 0; width = 12; height = 6
      },
      {
        type = "metric"
        properties = {
          title  = "RDS Performance"
          period = 60
          metrics = [
            ["AWS/RDS", "CPUUtilization",     "DBInstanceIdentifier", "${local.name_prefix}-postgres"],
            ["AWS/RDS", "DatabaseConnections","DBInstanceIdentifier", "${local.name_prefix}-postgres"],
            ["AWS/RDS", "ReadLatency",         "DBInstanceIdentifier", "${local.name_prefix}-postgres"],
            ["AWS/RDS", "WriteLatency",        "DBInstanceIdentifier", "${local.name_prefix}-postgres"],
          ]
          stat = "Average"
        }
        x = 0; y = 6; width = 12; height = 6
      },
      {
        type = "metric"
        properties = {
          title  = "ALB Request / Error Rate"
          period = 60
          metrics = [
            ["AWS/ApplicationELB", "RequestCount",            "LoadBalancer", "${local.name_prefix}-alb"],
            ["AWS/ApplicationELB", "HTTPCode_ELB_5XX_Count",  "LoadBalancer", "${local.name_prefix}-alb"],
            ["AWS/ApplicationELB", "HTTPCode_Target_4XX_Count","LoadBalancer", "${local.name_prefix}-alb"],
            ["AWS/ApplicationELB", "TargetResponseTime",       "LoadBalancer", "${local.name_prefix}-alb"],
          ]
          stat = "Sum"
        }
        x = 12; y = 6; width = 12; height = 6
      }
    ]
  })
}
