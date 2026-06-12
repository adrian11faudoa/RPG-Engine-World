# RealmForge Engine — AWS Deployment
### Full cloud infrastructure for the 3D Tabletop RPG Platform

---

## What This Deploys

```
                        ┌─────────────────────────────────────┐
                        │         realmforge.gg               │
  Players (browser) ───▶│  CloudFront CDN (TLS, gzip, cache)  │◀─── Assets (S3)
  GMs (browser)         └───────────────┬─────────────────────┘
                                        │
                              ┌─────────▼──────────┐
                              │  ALB (HTTPS 443)   │
                              └─────────┬──────────┘
                                        │
                              ┌─────────▼──────────┐
                              │   ECS Fargate       │  WebSocket /ws
                              │   Dashboard API     │  REST /api
                              │   Node.js 20        │
                              │   2 tasks (HA)      │
                              └──┬──────────┬───────┘
                                 │          │
                         ┌───────▼──┐  ┌───▼──────────┐
                         │ RDS PG15 │  │ Redis 7       │
                         │ Campaigns│  │ Sessions      │
                         │ Users    │  │ Presence      │
                         └──────────┘  └───────────────┘

  Players (UE5 client, UDP) ───▶ AWS GameLift Fleet (c5.large × N)
                                  UE5 Dedicated Server
                                  Auto-scales 1–10 instances
```

---

## Repository Structure

```
RealmForgeAWS/
├── terraform/                     ← Infrastructure as Code
│   ├── main.tf                    ← Root module, ties everything together
│   ├── variables.tf               ← All configurable inputs
│   ├── outputs.tf                 ← URLs, IDs, endpoints
│   ├── monitoring.tf              ← CloudWatch alarms + dashboard
│   └── modules/
│       ├── vpc/                   ← VPC, subnets, security groups
│       ├── ecs/                   ← Fargate, ALB, ECR, auto-scaling
│       ├── rds/                   ← PostgreSQL 15
│       ├── elasticache/           ← Redis 7
│       ├── gamelift/              ← Fleet, queue, matchmaking
│       └── cdn/                   ← CloudFront, S3, ACM, Route53
│
├── dashboard/                     ← Node.js API server (runs on ECS)
│   ├── src/
│   │   ├── server.js              ← Express API + WebSocket relay
│   │   └── rf-aws-client.js      ← Client-side SDK (for browser companion)
│   └── package.json
│
├── server/                        ← UE5 dedicated server AWS integration
│   ├── src/
│   │   ├── RFGameLiftServer.h/cpp ← GameLift SDK integration
│   │   └── RFAWSConnector.h/cpp  ← Client-side AWS connector
│   └── RealmForgeServer.sh       ← GameLift launch script
│
├── docker/
│   ├── dashboard/Dockerfile       ← Multi-stage production image
│   └── nginx/nginx.dev.conf      ← Local dev reverse proxy
│
├── docker-compose.yml             ← Full local dev stack (LocalStack)
├── scripts/
│   ├── db-init.sql                ← PostgreSQL schema
│   └── localstack-init.sh        ← Local AWS mock setup
│
├── .github/workflows/deploy.yml  ← CI/CD pipeline
└── docs/
    └── DEPLOYMENT_RUNBOOK.md      ← Step-by-step deployment guide
```

---

## Quick Start

### Local Development (5 minutes)

```bash
git clone https://github.com/your-org/RealmForgeAWS
cd RealmForgeAWS

# Start everything locally
docker compose up -d

# Dashboard API: http://localhost:3000
# Health check:  http://localhost:3000/health
# Adminer DB UI: http://localhost:8080 (add --profile debug)
# LocalStack:    http://localhost:4566
```

### Production Deployment (30 minutes)

```bash
# 1. Configure
cp terraform/envs/prod/terraform.tfvars.example terraform/envs/prod/terraform.tfvars
# Edit with your domain, passwords, etc.

# 2. Deploy infrastructure
cd terraform
terraform init && terraform apply -var-file=envs/prod/terraform.tfvars

# 3. Build and push dashboard
cd ../dashboard
ECR_URL=$(cd ../terraform && terraform output -raw ecr_repository_url)
aws ecr get-login-password | docker login --username AWS --password-stdin $ECR_URL
docker build -f ../docker/dashboard/Dockerfile -t $ECR_URL:latest .
docker push $ECR_URL:latest

# 4. Deploy to ECS
aws ecs update-service \
  --cluster realmforge-prod-cluster \
  --service realmforge-prod-dashboard-svc \
  --force-new-deployment

# 5. Full runbook: docs/DEPLOYMENT_RUNBOOK.md
```

---

## Environment Variables

| Variable | Required | Description |
|----------|----------|-------------|
| `DATABASE_URL` | ✅ | PostgreSQL connection string |
| `REDIS_URL` | ✅ | Redis connection string |
| `JWT_SECRET` | ✅ | 64-char random hex for JWT signing |
| `AWS_REGION` | ✅ | AWS region |
| `ASSETS_BUCKET` | ✅ | S3 bucket name for assets |
| `GAMELIFT_FLEET` | ✅ | Fleet ID from GameLift |
| `GAMELIFT_QUEUE` | ✅ | Queue name from GameLift |
| `CORS_ORIGIN` | ✅ | Comma-separated allowed origins |
| `DOMAIN` | ✅ | Public domain (realmforge.gg) |
| `PORT` | - | API port (default: 3000) |

---

## AWS Services Used

| Service | Purpose | Cost Driver |
|---------|---------|-------------|
| **GameLift** | Dedicated game servers | Instance hours |
| **ECS Fargate** | Dashboard API | vCPU + memory hours |
| **RDS PostgreSQL** | Campaign/user data | Instance hours |
| **ElastiCache Redis** | Session state | Instance hours |
| **ALB** | HTTPS load balancing | LCU hours |
| **CloudFront** | CDN for assets + dashboard | Data transfer |
| **S3** | Asset storage (maps, minis, audio) | Storage + requests |
| **ACM** | TLS certificates | Free |
| **Route53** | DNS | $0.50/zone/mo |
| **Secrets Manager** | DB + JWT secrets | $0.40/secret/mo |
| **CloudWatch** | Logs + alarms + dashboards | Log ingestion |

**Estimated monthly cost: ~$220 (prod) / ~$45 (dev/staging)**

---

## Security Checklist

- [x] All secrets in AWS Secrets Manager (never in code)
- [x] RDS in private subnets only (no public access)
- [x] Redis in private subnets only
- [x] ECS tasks run as non-root user (UID 1001)
- [x] ALB enforces HTTPS, redirects HTTP
- [x] CloudFront enforces TLS 1.2+
- [x] S3 bucket has public access blocked (CloudFront OAC)
- [x] ECR images scanned on push
- [x] RDS encrypted at rest + in transit
- [x] Rate limiting on API (300 req/15min, 20 auth/15min)
- [x] JWT validation on all protected routes
- [x] Server RPCs validate GM role before sensitive operations
- [x] GameLift player session validation on join

---

## Related Repositories

- [`RealmForgeEngine`](../RealmForgeEngine) — UE5 game source (C++ / Blueprints)
- `RealmForgeMods` — Community mod repository (planned)
- `RealmForgeCompanion` — Mobile companion app (planned)
