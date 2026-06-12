# RealmForge Engine — AWS Deployment Runbook
### Zero to Live in ~30 Minutes

---

## Architecture Overview

```
Players (game client, UDP)
         │
         ▼
  ┌─────────────────────────────────────────────────┐
  │              AWS GameLift Fleet                 │
  │   EC2 c5.large × N instances                    │
  │   UE5 Dedicated Server (port 7777 UDP/TCP)       │
  │   Auto-scales on active session %               │
  └─────────────────────────────────────────────────┘
         ▲
         │ CreatePlayerSession (server IP:port)
         │
Players (browser)                  GM (browser)
         │                              │
         ▼                              ▼
  ┌──────────────────────────────────────────────────┐
  │             CloudFront CDN                       │
  │   realmforge.gg  /  assets.realmforge.gg        │
  └────────────┬─────────────────────┬───────────────┘
               │ /api/*              │ /assets/*
               ▼                     ▼
  ┌────────────────────┐   ┌──────────────────────┐
  │   ALB (HTTPS 443)  │   │   S3 Assets Bucket   │
  └────────┬───────────┘   │   (maps, miniatures, │
           │               │    audio, mods)      │
           ▼               └──────────────────────┘
  ┌────────────────────┐
  │  ECS Fargate       │ ← WebSocket /ws
  │  Dashboard API     │
  │  Node.js           │
  │  2 tasks (HA)      │
  └────┬──────┬────────┘
       │      │
       ▼      ▼
  ┌────────┐ ┌──────────────┐
  │  RDS   │ │ ElastiCache  │
  │  PG 15 │ │ Redis 7      │
  └────────┘ └──────────────┘
```

---

## Prerequisites

```bash
# Install tools
brew install terraform awscli
npm install -g aws-cdk   # optional

# Verify
terraform --version   # >= 1.6
aws --version         # >= 2.x

# Configure AWS credentials
aws configure
# Enter: Access Key, Secret Key, Region (us-east-1), Output (json)

# Verify access
aws sts get-caller-identity
```

---

## Step 1: Bootstrap Terraform State

This only runs once ever.

```bash
# Create S3 bucket for Terraform state
aws s3 mb s3://realmforge-terraform-state --region us-east-1
aws s3api put-bucket-versioning \
  --bucket realmforge-terraform-state \
  --versioning-configuration Status=Enabled
aws s3api put-bucket-encryption \
  --bucket realmforge-terraform-state \
  --server-side-encryption-configuration \
  '{"Rules":[{"ApplyServerSideEncryptionByDefault":{"SSEAlgorithm":"AES256"}}]}'

# Create DynamoDB lock table
aws dynamodb create-table \
  --table-name realmforge-tf-lock \
  --attribute-definitions AttributeName=LockID,AttributeType=S \
  --key-schema AttributeName=LockID,KeyType=HASH \
  --billing-mode PAY_PER_REQUEST \
  --region us-east-1
```

---

## Step 2: Configure Variables

```bash
cd terraform

# Create prod vars file (never commit this)
cat > envs/prod/terraform.tfvars << 'EOF'
aws_region   = "us-east-1"
environment  = "prod"
domain_name  = "realmforge.gg"     # ← your domain
db_password  = "CHANGE_ME_STRONG_PASSWORD_32CHARS"
jwt_secret   = "CHANGE_ME_RANDOM_64_CHAR_HEX_STRING"
dashboard_ecr_image = "placeholder"  # filled after ECR is created
EOF

# Generate secure secrets
openssl rand -hex 32  # use for db_password
openssl rand -hex 64  # use for jwt_secret
```

---

## Step 3: Deploy Infrastructure

```bash
cd terraform

# Init (downloads providers, configures backend)
terraform init

# Preview what will be created (~45 resources)
terraform plan -var-file=envs/prod/terraform.tfvars

# Apply (takes ~15 minutes for RDS, GameLift, CloudFront)
terraform apply -var-file=envs/prod/terraform.tfvars

# Save outputs
terraform output -json > ../outputs.json
cat ../outputs.json
```

Key outputs you'll need:
- `ecr_repository_url` → for Docker push
- `game_server_fleet_id` → for game client config
- `assets_bucket` → for asset uploads
- `dashboard_url` → public URL

---

## Step 4: Build & Push Dashboard Image

```bash
cd dashboard

# Get ECR URL from Terraform output
ECR_URL=$(cd ../terraform && terraform output -raw ecr_repository_url)
AWS_REGION=us-east-1
AWS_ACCOUNT=$(aws sts get-caller-identity --query Account --output text)

# Authenticate Docker to ECR
aws ecr get-login-password --region ${AWS_REGION} | \
  docker login --username AWS --password-stdin "${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com"

# Build image
docker build -f ../docker/dashboard/Dockerfile -t realmforge-dashboard .

# Tag and push
docker tag realmforge-dashboard:latest "${ECR_URL}:latest"
docker push "${ECR_URL}:latest"

echo "Image pushed: ${ECR_URL}:latest"
```

---

## Step 5: Update ECS with New Image

```bash
cd terraform

# Update the image variable
sed -i "s|dashboard_ecr_image = \"placeholder\"|dashboard_ecr_image = \"${ECR_URL}:latest\"|" \
  envs/prod/terraform.tfvars

# Re-apply (only ECS task definition changes)
terraform apply -var-file=envs/prod/terraform.tfvars -target=module.ecs

# Force new deployment
aws ecs update-service \
  --cluster realmforge-prod-cluster \
  --service realmforge-prod-dashboard-svc \
  --force-new-deployment \
  --region us-east-1

# Watch rollout (Ctrl+C when stable)
aws ecs wait services-stable \
  --cluster realmforge-prod-cluster \
  --services realmforge-prod-dashboard-svc
```

---

## Step 6: Deploy Game Server to GameLift

```bash
# Build your UE5 Linux server first (requires UE5 + Linux cross-compile toolchain)
# See: https://docs.unrealengine.com/5.0/en-US/linux-development-requirements/

# Package it
cd server
zip -r RealmForgeServer-Linux.zip build/LinuxServer/ RealmForgeServer.sh

# Get build bucket from Terraform
BUILD_BUCKET=$(cd ../terraform && terraform output -raw game_server_build_bucket 2>/dev/null \
  || echo "realmforge-prod-gamelift-builds")
BUILD_ROLE=$(aws iam get-role --role-name realmforge-prod-gamelift-build-role \
  --query 'Role.Arn' --output text)

# Upload to S3
aws s3 cp RealmForgeServer-Linux.zip \
  "s3://${BUILD_BUCKET}/server-builds/RealmForgeServer-Linux.zip"

# Create GameLift build
BUILD_ID=$(aws gamelift create-build \
  --name "RealmForge-v1.0" \
  --operating-system AMAZON_LINUX_2 \
  --storage-location "Bucket=${BUILD_BUCKET},Key=server-builds/RealmForgeServer-Linux.zip,RoleArn=${BUILD_ROLE}" \
  --region us-east-1 \
  --query 'Build.BuildId' --output text)

echo "Build created: ${BUILD_ID}"

# Wait for it to be ready
aws gamelift wait build-ready --build-id ${BUILD_ID}
echo "Build is READY — update fleet via Terraform or Console"
```

---

## Step 7: Point Your Domain

In your domain registrar, set nameservers to Route53:

```bash
# Get Route53 nameservers for your zone
aws route53 get-hosted-zone \
  --id $(aws route53 list-hosted-zones --query "HostedZones[?Name=='realmforge.gg.'].Id" --output text) \
  --query 'DelegationSet.NameServers'
```

Set those 4 NS records at your registrar. DNS propagates in ~15 minutes.

---

## Step 8: Verify Everything Works

```bash
# Health check
curl https://realmforge.gg/health
# Expected: {"status":"ok","timestamp":"..."}

# Test auth
curl -X POST https://realmforge.gg/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"testgm","email":"gm@example.com","password":"TestPass123!"}'

# Test WebSocket
wscat -c wss://realmforge.gg/ws
# Send: {"type":"ping"}
# Expect: {"type":"pong"}

# Test GameLift fleet
aws gamelift describe-fleet-attributes \
  --fleet-ids $(cd terraform && terraform output -raw game_server_fleet_id) \
  --query 'FleetAttributes[0].Status'
# Expected: "ACTIVE"
```

---

## Ongoing Operations

### Scale game server fleet
```bash
aws gamelift update-fleet-capacity \
  --fleet-id <fleet-id> \
  --desired-instances 3 \
  --min-size 1 \
  --max-size 10
```

### View logs
```bash
# Dashboard API logs
aws logs tail /ecs/realmforge-prod-dashboard --follow

# GameLift server logs (from S3 after session ends)
aws s3 ls s3://realmforge-prod-gamelift-builds/fleets/

# ECS service events
aws ecs describe-services \
  --cluster realmforge-prod-cluster \
  --services realmforge-prod-dashboard-svc \
  --query 'services[0].events[:5]'
```

### Database backup
```bash
# Create manual snapshot
aws rds create-db-snapshot \
  --db-instance-identifier realmforge-prod-postgres \
  --db-snapshot-identifier realmforge-manual-$(date +%Y%m%d)
```

### Tear down everything
```bash
cd terraform
terraform destroy -var-file=envs/prod/terraform.tfvars
# ⚠️  This deletes ALL data. RDS has deletion_protection=true in prod.
# You must disable it first: aws rds modify-db-instance --deletion-protection false
```

---

## Cost Estimate (monthly)

| Service | Size | Est. Monthly |
|---------|------|-------------|
| GameLift Fleet | 2× c5.large | ~$140 |
| ECS Fargate | 2× 0.5vCPU / 1GB | ~$20 |
| RDS PostgreSQL | db.t3.micro | ~$15 |
| ElastiCache Redis | cache.t3.micro | ~$15 |
| ALB | - | ~$18 |
| CloudFront | 100GB transfer | ~$9 |
| S3 Assets | 50GB | ~$1 |
| **Total** | | **~$220/mo** |

*Tip: Use FARGATE_SPOT for dashboard non-critical tasks to cut ECS cost by 70%.*

---

## Secrets Management Reference

| Secret | Location | Used By |
|--------|----------|---------|
| DB password | Secrets Manager `/realmforge/prod/db-password` | ECS task |
| JWT secret | Secrets Manager `/realmforge/prod/jwt-secret` | ECS task |
| DB URL | SSM Parameter `/realmforge/prod/db-url` | GameLift server |
| Redis URL | SSM Parameter `/realmforge/prod/redis-url` | GameLift server |

**Never put secrets in:**
- Environment variables in plain Terraform `tfvars` (use `-var` on CLI or CI secrets)
- Docker images
- Git commits (use `.gitignore` on `.tfvars` files)

---

*RealmForge Engine AWS Runbook — v1.0*
