#!/bin/bash
##############################################################
# LocalStack Initialization Script
# Creates mock AWS resources for local development
# Runs automatically when LocalStack starts
##############################################################

set -e

echo "[LocalStack] Initializing RealmForge resources..."

REGION="us-east-1"
ENDPOINT="http://localhost:4566"

# Helper
awslocal() {
  aws --endpoint-url="${ENDPOINT}" --region="${REGION}" "$@"
}

# ─── S3 Buckets ────────────────────────────────────────────────
echo "[LocalStack] Creating S3 buckets..."

awslocal s3 mb s3://realmforge-local-assets
awslocal s3 mb s3://realmforge-local-gamelift-builds

# Enable CORS on assets bucket
awslocal s3api put-bucket-cors \
  --bucket realmforge-local-assets \
  --cors-configuration '{
    "CORSRules": [{
      "AllowedOrigins": ["*"],
      "AllowedMethods": ["GET", "HEAD", "PUT", "POST"],
      "AllowedHeaders": ["*"],
      "MaxAgeSeconds": 3600
    }]
  }'

echo "[LocalStack] S3 buckets created"

# ─── Secrets Manager ───────────────────────────────────────────
echo "[LocalStack] Creating secrets..."

awslocal secretsmanager create-secret \
  --name "/realmforge/dev/db-password" \
  --secret-string "devpass" 2>/dev/null || true

awslocal secretsmanager create-secret \
  --name "/realmforge/dev/jwt-secret" \
  --secret-string "dev-jwt-secret-change-in-prod-64chars-padding-here" 2>/dev/null || true

echo "[LocalStack] Secrets created"

# ─── SSM Parameters ────────────────────────────────────────────
echo "[LocalStack] Creating SSM parameters..."

awslocal ssm put-parameter \
  --name "/realmforge/dev/db-url" \
  --type "SecureString" \
  --value "postgresql://realmforge:devpass@postgres:5432/realmforge" \
  --overwrite 2>/dev/null || true

awslocal ssm put-parameter \
  --name "/realmforge/dev/redis-url" \
  --type "SecureString" \
  --value "redis://redis:6379" \
  --overwrite 2>/dev/null || true

echo "[LocalStack] SSM parameters created"

# ─── Upload sample assets ──────────────────────────────────────
echo "[LocalStack] Seeding sample assets..."

# Create placeholder files
echo '{"type":"map","name":"Dungeon of Shadows"}' > /tmp/sample-map.json
awslocal s3 cp /tmp/sample-map.json s3://realmforge-local-assets/maps/sample/dungeon-of-shadows.json

echo "[LocalStack] ✅ All resources initialized"
echo "[LocalStack] S3 assets: http://localhost:4566/realmforge-local-assets"
