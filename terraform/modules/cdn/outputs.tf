output "cloudfront_domain" {
  description = "CloudFront distribution domain name"
  value       = aws_cloudfront_distribution.main.domain_name
}

output "cloudfront_id" {
  description = "CloudFront distribution ID (for cache invalidation)"
  value       = aws_cloudfront_distribution.main.id
}

output "assets_bucket_name" {
  description = "S3 assets bucket name"
  value       = aws_s3_bucket.assets.bucket
}

output "assets_bucket_arn" {
  description = "S3 assets bucket ARN"
  value       = aws_s3_bucket.assets.arn
}

output "certificate_arn" {
  description = "ACM certificate ARN (us-east-1) for CloudFront and ALB"
  value       = aws_acm_certificate.main.arn
}

output "cloudfront_arn" {
  description = "CloudFront distribution ARN — used for WAF association"
  value       = aws_cloudfront_distribution.main.arn
}
