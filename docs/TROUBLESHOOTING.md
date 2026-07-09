# RealmForge Engine — Troubleshooting Guide

---

## Local Development

### `docker compose up` fails with "port already in use"

```bash
# Find what's using the port
lsof -i :5432   # postgres
lsof -i :6379   # redis
lsof -i :3000   # dashboard
lsof -i :4566   # localstack

# Kill the process or change the port in docker-compose.yml
```

### Dashboard container exits immediately

```bash
# Check logs for the error
docker compose logs dashboard

# Common causes:
# 1. DATABASE_URL misconfigured — postgres not ready yet
docker compose restart dashboard   # postgres needs ~10s to init

# 2. Missing JWT_SECRET
# Check dashboard/.env or docker-compose.yml environment section

# 3. Port 3000 in use on host
lsof -i :3000
```

### LocalStack not initialising properly

```bash
# Check LocalStack health
curl http://localhost:4566/_localstack/health

# Re-run init script
docker compose exec localstack bash /etc/localstack/init/ready.d/init.sh

# Check init script logs
docker compose logs localstack | grep -E "ERROR|WARN|init"

# Nuclear option — delete LocalStack volume and restart
docker compose down -v
docker compose up -d localstack
sleep 30
docker compose up -d
```

### `curl http://localhost:3000/health` returns connection refused

```bash
# Is the container running?
docker compose ps

# Is it healthy?
docker compose ps --format "table {{.Name}}\t{{.Status}}"

# Check if dashboard is listening
docker compose exec dashboard netstat -tlnp | grep 3000

# Check for startup errors
docker compose logs --tail=50 dashboard
```

### Database migration errors

```bash
# Reset and rerun from scratch
docker compose exec postgres psql -U realmforge realmforge \
  -c "DROP SCHEMA public CASCADE; CREATE SCHEMA public;"

docker compose exec dashboard node src/db/migrate.js
```

---

## Terraform / AWS

### `terraform init` fails — backend bucket doesn't exist

```bash
# Create the state bucket first
aws s3 mb s3://realmforge-terraform-state --region us-east-1
aws s3api put-bucket-versioning \
  --bucket realmforge-terraform-state \
  --versioning-configuration Status=Enabled

# Create DynamoDB lock table
aws dynamodb create-table \
  --table-name realmforge-tf-lock \
  --attribute-definitions AttributeName=LockID,AttributeType=S \
  --key-schema AttributeName=LockID,KeyType=HASH \
  --billing-mode PAY_PER_REQUEST

terraform init
```

### `terraform apply` fails with "Error: No valid credential sources found"

```bash
# Verify AWS credentials
aws sts get-caller-identity

# If using profiles
export AWS_PROFILE=realmforge-deploy
aws sts get-caller-identity

# If using IAM role
aws configure
```

### `terraform apply` fails at ACM certificate — "PENDING_VALIDATION"

The certificate needs DNS validation records. Terraform creates the Route53 records automatically, but if your domain is **not** in Route53, you need to:

1. Note the CNAME record from the error output
2. Add it manually at your domain registrar
3. Wait for DNS propagation (up to 48 hours)
4. Re-run `terraform apply`

### RDS can't be deleted — "deletion protection enabled"

```bash
# Disable protection first
aws rds modify-db-instance \
  --db-instance-identifier realmforge-prod-postgres \
  --no-deletion-protection \
  --apply-immediately

# Then destroy
terraform destroy -target=module.rds
```

### Terraform state lock stuck

```bash
# Force-unlock (get lock ID from the error message)
terraform force-unlock <LOCK_ID>
```

---

## ECS / Dashboard

### ECS service stuck in DRAINING / not deploying

```bash
# Check service events
aws ecs describe-services \
  --cluster realmforge-prod-cluster \
  --services realmforge-prod-dashboard-svc \
  --query 'services[0].events[:10]'

# Check task failures
aws ecs list-tasks \
  --cluster realmforge-prod-cluster \
  --desired-status STOPPED \
  --query 'taskArns[:5]'

# Get failure reason for a stopped task
aws ecs describe-tasks \
  --cluster realmforge-prod-cluster \
  --tasks <TASK_ARN> \
  --query 'tasks[0].containers[0].reason'
```

### ECS task failing health check

```bash
# Check what the container actually returns
aws logs tail /ecs/realmforge-prod-dashboard --since 5m --follow

# Common causes:
# 1. DATABASE_URL wrong — connection refused to RDS
# 2. JWT_SECRET not in Secrets Manager
# 3. Application crashed on startup (check logs)

# Test health endpoint directly (requires VPN / bastion)
curl http://<TASK_PRIVATE_IP>:3000/health
```

### "No space left in ECR" / push fails

```bash
# Manually run lifecycle policy
aws ecr batch-delete-image \
  --repository-name realmforge-prod-dashboard \
  --image-ids "$(aws ecr describe-images \
    --repository-name realmforge-prod-dashboard \
    --query 'sort_by(imageDetails, &imagePushedAt)[:-10].{imageDigest: imageDigest}' \
    --output json)"
```

---

## GameLift

### Fleet stuck in ACTIVATING

```bash
# Check fleet events
aws gamelift describe-fleet-events \
  --fleet-id <FLEET_ID> \
  --query 'Events[-5:]'

# Common causes:
# 1. Server binary not executable
chmod +x server/RealmForgeServer.sh

# 2. Build zip structure wrong — GameLift expects:
#    RealmForgeServer.sh at root
#    Binary at: /local/game/RealmForgeServer

# 3. Launch path mismatch — check runtime configuration
aws gamelift describe-runtime-configuration --fleet-id <FLEET_ID>
```

### "No available game sessions" when trying to join

```bash
# Check fleet capacity
aws gamelift describe-fleet-capacity --fleet-ids <FLEET_ID>

# Check active sessions
aws gamelift describe-game-sessions \
  --fleet-id <FLEET_ID> \
  --status-filter ACTIVE

# Manually increase desired instances
aws gamelift update-fleet-capacity \
  --fleet-id <FLEET_ID> \
  --desired-instances 2
```

### Player session rejected by server

The UE5 server must call `AcceptPlayerSession(playerSessionId)` within 60 seconds of the player connecting. Check:

1. `URFGameLiftServer::AcceptPlayerSession` is being called in `APlayerController::PreLogin` or `PostLogin`
2. The `playerSessionId` is passed from the client to the server (via URL param or custom auth)
3. The GameLift SDK is initialised — check `bGameLiftActive == true` in logs

---

## WebSocket

### WS connection drops every few minutes

The ALB has a default 60-second idle timeout. Increase it:

```bash
aws elbv2 modify-load-balancer-attributes \
  --load-balancer-arn <ALB_ARN> \
  --attributes Key=idle_timeout.timeout_seconds,Value=3600
```

Also ensure the client sends a ping every 30 seconds (already implemented in `RealmForgeSocket`).

### WS messages not reaching all players

Multiple ECS tasks don't share memory. If Task 1 has Player A's connection and Task 2 has Player B's, a message on Task 1 won't reach Player B.

**Fix:** The `broadcast()` function must use Redis pub/sub to cross task boundaries. Add to `server.js`:

```js
const redisPub = new Redis(REDIS_URL);
const redisSub = new Redis(REDIS_URL);

// Subscribe to cross-task messages
redisSub.subscribe('rf:broadcast');
redisSub.on('message', (channel, raw) => {
  const { sessionId, message, excludeUserId } = JSON.parse(raw);
  const local = connections.get(sessionId);
  local?.forEach(ws => {
    if (ws.userId !== excludeUserId && ws.readyState === 1) ws.send(message);
  });
});

// Update broadcast() to publish via Redis
function broadcast(sessionId, message, excludeWs = null) {
  redisPub.publish('rf:broadcast', JSON.stringify({
    sessionId, message: JSON.stringify(message),
    excludeUserId: excludeWs?.userId || null
  }));
}
```

---

## Common Error Messages

| Error | Cause | Fix |
|-------|-------|-----|
| `ECONNREFUSED 5432` | Postgres not running | `docker compose up -d postgres` |
| `ECONNREFUSED 6379` | Redis not running | `docker compose up -d redis` |
| `JsonWebTokenError: invalid signature` | Wrong JWT_SECRET | Check env vars match between services |
| `GameSessionFullException` | Session at max players | Join a different session or wait |
| `InvalidRequestException: Fleet not active` | Fleet still activating | Wait 5–10 min after fleet creation |
| `CredentialsProviderError` | No AWS credentials | `aws configure` or check IAM role |
| `ResourceNotFoundException` | Secret/param doesn't exist in correct region | Check AWS_REGION |
| `ERR WRONGTYPE` | Redis key type conflict | `redis-cli flushdb` (dev only!) |
