# RealmForge Engine — Architecture Documentation

## System Overview

RealmForge runs as two distinct runtimes that communicate:

1. **Game Client + Dedicated Server** — Unreal Engine 5, peer-to-server UDP via AWS GameLift
2. **Web Dashboard + API** — Node.js on ECS Fargate, HTTPS/WebSocket via CloudFront → ALB

```
┌─────────────────────────────────────────────────────────────────────┐
│                        PLAYER DEVICES                                │
│                                                                     │
│   ┌────────────────────┐          ┌──────────────────────────────┐  │
│   │  UE5 Game Client   │          │  Browser (Campaign Dashboard) │  │
│   │  (Windows/Linux)   │          │  Campaign mgmt, journal,     │  │
│   │  3D tabletop view  │          │  asset upload, session lobby │  │
│   └────────┬───────────┘          └──────────────┬───────────────┘  │
└────────────┼──────────────────────────────────────┼─────────────────┘
             │ UDP 7777                             │ HTTPS / WSS
             ▼                                      ▼
┌────────────────────────┐   ┌──────────────────────────────────────┐
│   AWS GameLift Fleet   │   │         AWS CloudFront CDN           │
│   EC2 c5.large × 1–10 │   │   realmforge.gg (apex + www)         │
│   UE5 Dedicated Server │   │   Cache-Control per path:            │
│   4 sessions/instance  │   │   /api/*  → no-cache → ALB           │
│   Auto-scales on       │   │   /assets/* → 1yr → S3               │
│   session utilisation  │   │   /*       → pass-through → ALB      │
└────────────────────────┘   └───────────────┬──────────────────────┘
                                             │
                             ┌───────────────▼──────────────────────┐
                             │     Application Load Balancer         │
                             │     HTTPS 443 (TLS 1.2+)             │
                             │     HTTP 80 → redirect               │
                             └───────────────┬──────────────────────┘
                                             │
                             ┌───────────────▼──────────────────────┐
                             │       ECS Fargate Cluster             │
                             │  ┌─────────────┐ ┌─────────────┐    │
                             │  │  Task 1     │ │  Task 2     │    │  ← 2 tasks (prod HA)
                             │  │  Node.js    │ │  Node.js    │    │
                             │  │  :3000      │ │  :3000      │    │
                             │  └──────┬──────┘ └──────┬──────┘    │
                             └─────────┼────────────────┼───────────┘
                                       │                │
                              ┌────────▼──────┐  ┌──────▼──────────┐
                              │  RDS PG 15    │  │ ElastiCache      │
                              │  (private)    │  │ Redis 7          │
                              │  campaigns    │  │ sessions         │
                              │  users        │  │ websocket state  │
                              │  journal      │  │ fog/initiative   │
                              │  roll_log     │  │ player presence  │
                              └───────────────┘  └─────────────────┘

                             ┌──────────────────────────────────────┐
                             │       S3 Assets Bucket               │
                             │  maps/, miniatures/, audio/, mods/   │
                             │  Served via CloudFront OAC           │
                             │  Uploaded via presigned URLs         │
                             └──────────────────────────────────────┘
```

---

## Request Flows

### Flow 1: Player Logs In and Joins a Session

```
Browser                    CloudFront           ECS (Node.js)         RDS
   │                           │                     │                  │
   │─── POST /api/auth/login ─▶│─── forward ────────▶│                  │
   │                           │                     │─ SELECT users ──▶│
   │                           │                     │◀─ user row ──────│
   │                           │                     │─ bcrypt verify   │
   │                           │                     │─ sign JWT        │
   │◀── 200 { token, user } ───│◀───────────────────-│                  │
   │                           │                     │                  │
   │─── GET /api/sessions ────▶│─── forward ────────▶│                  │
   │                           │                     │─ GameLift.Search │
   │◀── 200 [sessions list] ───│◀────────────────────│                  │
   │                           │                     │                  │
   │─── POST /api/sessions/:id/join ────────────────▶│                  │
   │                           │                     │─ GameLift.CreatePlayerSession
   │◀── { serverEndpoint } ────│◀────────────────────│                  │
   │                           │                     │                  │
   │══════════ UE5 client connects to serverEndpoint (UDP) ════════════▶│ GameLift
```

### Flow 2: GM Updates Fog, All Players See It

```
GM Browser              WebSocket (ECS)          Player Browsers
    │                        │                         │
    │── ws send fog_update ─▶│                         │
    │                        │─ redis.set fog:sessionId│
    │                        │─ broadcast to all ──────▶│ (all connected WS clients)
    │                        │                         │── UE5Bridge.send(fog_updated)
    │                        │                         │── URFFogOfWar.RevealCells(...)
```

### Flow 3: Asset Upload (Map File)

```
GM Browser         CloudFront         ECS Node.js          S3
    │                  │                   │                 │
    │─ POST /api/assets/upload-url ────────▶│                 │
    │                  │                   │─ PutObjectCmd   │
    │                  │                   │─ getSignedUrl   │
    │◀─ { uploadUrl, key, cdnUrl } ────────│                 │
    │                  │                   │                 │
    │─────────────────── PUT uploadUrl (directly to S3) ────▶│
    │                  │                   │                 │
    │◀──────────────────── 200 OK ─────────────────────────── │
    │                  │                   │                 │
    │  cdnUrl = https://realmforge.gg/assets/maps/user/uuid-mapname.umap
```

---

## Technology Decisions

### Why GameLift vs self-managed EC2?

| | GameLift | Self-managed EC2 |
|--|---------|-----------------|
| Auto-scaling | ✅ Built-in, session-aware | Manual, lag-prone |
| Server placement | ✅ Latency-based routing | Manual regions |
| Player sessions | ✅ Validated by SDK | Custom auth needed |
| Spot interruption | ✅ Graceful handling | DIY |
| Cost | Pay per active session | Pay 24/7 |
| Complexity | GameLift SDK in UE5 | Just EC2 |

GameLift wins for a game server workload. The added SDK complexity is worth the operational savings.

### Why ECS Fargate vs Lambda for the API?

- WebSocket connections are long-lived (hours per session) — Lambda 15-min timeout rules it out
- Fargate containers keep in-memory WebSocket state between messages
- Predictable latency vs Lambda cold starts for game-time API calls
- Easy Docker deployment matches dev workflow

### Why PostgreSQL vs DynamoDB?

- Campaign data is relational (users → campaigns → sessions → rolls)
- Complex JOIN queries for dashboard views (campaign + player count + last session)
- JSONB for flexible world_state without schema migrations
- Familiar SQL for future contributors

### Why Redis for WebSocket state?

- Multiple ECS tasks need shared session membership — Redis pub/sub bridges them
- Fog of war and initiative data need sub-millisecond reads between turns
- Player presence (online/offline) expires naturally via Redis TTL

### Why CloudFront in front of everything?

- Single domain for both API and assets simplifies CORS
- Asset caching slashes S3 costs and latency for maps/audio/miniatures
- WAF can be attached to CloudFront in one place
- Free TLS via ACM

---

## Networking & Security Layers

```
Internet
  │
  ▼ CloudFront (TLS 1.2+, WAF optional)
  │
  ▼ ALB (security group: only from CloudFront)
  │
  ▼ ECS Tasks in private subnets
  │       (security group: only port 3000 from ALB SG)
  ├──▶ RDS in database subnets
  │       (security group: only 5432 from ECS SG)
  └──▶ Redis in private subnets
          (security group: only 6379 from ECS SG)
```

**Secrets flow:**
- `db_password`, `jwt_secret` → Secrets Manager → ECS task at runtime via `secrets:` in task definition (never in env vars at rest)
- GameLift server reads DB/Redis URLs from SSM Parameter Store via IAM role (no credentials in the binary)

---

## Scaling Characteristics

| Component | Scale Trigger | Min | Max |
|-----------|-------------|-----|-----|
| ECS Tasks | CPU > 70% or Memory > 80% | 2 | 10 |
| GameLift Fleet | Active session % > 70% | 1 | 10 |
| RDS | Manual (change instance class) | — | — |
| Redis | Manual (change node type) | — | — |
| CloudFront | Automatic (managed) | — | ∞ |

**Bottleneck order under load:**
1. Redis (single node) — upgrade to cluster mode if >1000 concurrent sessions
2. RDS write throughput — add read replica for dashboard queries
3. ECS memory — WebSocket connections ~2MB each; 500 concurrent = 1GB

---

## Monitoring & Alerting

CloudWatch alarms notify `alert_email` on:

| Alarm | Threshold | Action |
|-------|-----------|--------|
| ECS CPU High | > 85% for 2 min | Scale out |
| RDS CPU High | > 80% for 3 min | Investigate query |
| RDS Storage Low | < 2 GB | Add storage |
| ALB 5xx Rate | > 20/min | Page on-call |
| GameLift Sessions Full | > 35 active | Add fleet capacity |

Dashboard: CloudWatch → `realmforge-prod-ops`

---

## Disaster Recovery

| Scenario | RTO | RPO | Procedure |
|----------|-----|-----|-----------|
| ECS task crash | <60s | 0 | ECS replaces automatically |
| ECS deploy failure | <5min | 0 | Circuit-breaker auto-rollback |
| RDS failure | <2min | <5min | Multi-AZ failover (enable for prod) |
| GameLift instance fail | <30s | 0 | Fleet replaces instance |
| Region outage | Manual | <1day | Restore RDS snapshot in new region |

Backups:
- RDS: automated daily snapshots, 7-day retention
- S3 assets: versioning enabled
- Redis: hourly snapshots, 1-day retention
