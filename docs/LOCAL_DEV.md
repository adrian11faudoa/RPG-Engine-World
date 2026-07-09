# RealmForge Engine — Local Development Guide

## Prerequisites

| Tool | Version | Install |
|------|---------|---------|
| Docker Desktop | 4.x+ | https://docker.com/products/docker-desktop |
| Node.js | 18+ | https://nodejs.org or `nvm install 20` |
| AWS CLI | 2.x | `brew install awscli` |
| Terraform | 1.6+ | `brew install terraform` |
| wscat (optional) | any | `npm i -g wscat` (for WS debugging) |

---

## Quick Start (5 minutes)

```bash
# 1. Clone
git clone https://github.com/your-org/RealmForgeAWS
cd RealmForgeAWS

# 2. Copy env file
cp dashboard/.env.example dashboard/.env
# No changes needed — defaults work with docker-compose

# 3. Start everything
docker compose up -d

# 4. Wait ~30s for LocalStack and Postgres to initialise, then:
curl http://localhost:3000/health
# → {"status":"ok","timestamp":"..."}
```

That's it. The full stack is running locally.

---

## What Runs Locally

| Service | Port | URL |
|---------|------|-----|
| Dashboard API | 3000 | http://localhost:3000 |
| nginx reverse proxy | 80 | http://localhost |
| PostgreSQL | 5432 | localhost:5432 / db: realmforge |
| Redis | 6379 | localhost:6379 |
| LocalStack (AWS mock) | 4566 | http://localhost:4566 |
| Adminer (DB UI) | 8080 | http://localhost:8080 *(debug profile)* |
| Redis Commander | 8081 | http://localhost:8081 *(debug profile)* |

### Start debug tools
```bash
docker compose --profile debug up -d
```

---

## Making Code Changes

The `dashboard/src` directory is mounted into the container as a volume — code changes **hot-reload automatically** (nodemon is watching).

```bash
# Edit a file
vim dashboard/src/routes/campaigns.js

# See it reload in logs
docker compose logs -f dashboard
```

---

## Running Tests

```bash
# Option A: inside Docker (matches CI exactly)
docker compose exec dashboard npm test

# Option B: locally (faster iteration)
cd dashboard
npm install
DATABASE_URL=postgresql://realmforge:devpass@localhost:5432/realmforge \
REDIS_URL=redis://localhost:6379 \
JWT_SECRET=local-test-secret \
npm test
```

---

## Database

### Access via Adminer
1. Start debug profile: `docker compose --profile debug up -d`
2. Open http://localhost:8080
3. Login: Server=`postgres`, User=`realmforge`, Password=`devpass`, Database=`realmforge`

### Access via psql
```bash
psql postgresql://realmforge:devpass@localhost:5432/realmforge
```

### Run migrations
```bash
docker compose exec dashboard node src/db/migrate.js
```

### Reset database
```bash
docker compose down -v          # removes volumes
docker compose up -d            # re-creates and re-runs init.sql
```

---

## Working with LocalStack (Mock AWS)

All AWS calls are automatically redirected to LocalStack in local dev via `AWS_ENDPOINT_URL=http://localstack:4566`.

### View S3 buckets
```bash
aws --endpoint-url=http://localhost:4566 s3 ls
# → realmforge-local-assets
# → realmforge-local-gamelift-builds
```

### Upload a test asset manually
```bash
aws --endpoint-url=http://localhost:4566 \
  s3 cp ./my-map.png s3://realmforge-local-assets/maps/test/my-map.png
```

### View secrets
```bash
aws --endpoint-url=http://localhost:4566 \
  secretsmanager list-secrets
```

---

## Testing the WebSocket

```bash
# Connect
wscat -c ws://localhost:3000/ws

# Authenticate (paste and hit Enter)
{"type":"auth","token":"<JWT from /api/auth/login>"}

# Join a session
{"type":"join_session","sessionId":"test-session-123"}

# Send a dice roll
{"type":"dice_roll","formula":"1d20","result":{"total":17},"visibility":"public"}

# Ping
{"type":"ping"}
```

---

## Common Commands

```bash
# View all logs
docker compose logs -f

# View just dashboard logs
docker compose logs -f dashboard

# Restart dashboard (after package.json changes)
docker compose restart dashboard

# Rebuild image (after Dockerfile changes)
docker compose build dashboard && docker compose up -d dashboard

# Stop everything
docker compose down

# Stop and delete all data
docker compose down -v

# Shell into dashboard container
docker compose exec dashboard sh

# Shell into postgres
docker compose exec postgres psql -U realmforge realmforge
```

---

## Environment Variables

All variables have sensible local defaults in `docker-compose.yml`.
For local-only overrides, edit `dashboard/.env` (gitignored):

```bash
# dashboard/.env
NODE_ENV=development
PORT=3000
DATABASE_URL=postgresql://realmforge:devpass@localhost:5432/realmforge
REDIS_URL=redis://localhost:6379
JWT_SECRET=dev-jwt-secret-change-in-production
AWS_REGION=us-east-1
ASSETS_BUCKET=realmforge-local-assets
GAMELIFT_FLEET=local-fleet
CORS_ORIGIN=http://localhost:5173,http://localhost:3000
AWS_ENDPOINT_URL=http://localhost:4566
AWS_ACCESS_KEY_ID=test
AWS_SECRET_ACCESS_KEY=test
```

---

## Connecting a UE5 Client Locally

The UE5 client's `URFAWSConnector` can point to localhost:

In `DefaultGame.ini` (or at runtime via Blueprint):
```ini
[/Script/RealmForge.RFAWSConnector]
APIBaseURL=http://localhost:3000/api
WSBaseURL=ws://localhost:3000/ws
CDNBaseURL=http://localhost:4566/realmforge-local-assets
```

The game session will return `serverEndpoint=127.0.0.1:7777` which you can connect to if you're running the UE5 dedicated server locally too.

---

## Troubleshooting Local Dev

See [TROUBLESHOOTING.md](./TROUBLESHOOTING.md) for solutions to common issues.
