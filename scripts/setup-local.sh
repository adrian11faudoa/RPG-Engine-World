#!/bin/bash
##############################################################
# scripts/setup-local.sh
# First-time local development environment setup.
# Run once after cloning the repo.
##############################################################

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

info()    { echo -e "${GREEN}[setup]${NC} $*"; }
warn()    { echo -e "${YELLOW}[setup]${NC} $*"; }
error()   { echo -e "${RED}[setup]${NC} $*" >&2; }
heading() { echo -e "\n${GREEN}══ $* ══${NC}"; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

heading "RealmForge Engine — Local Dev Setup"

# ─── Prerequisites ─────────────────────────────────────────────
heading "Checking prerequisites"

check_cmd() {
    if command -v "$1" &>/dev/null; then
        info "✅ $1 found ($(command -v "$1"))"
    else
        error "❌ $1 not found — please install it"
        MISSING_DEPS=1
    fi
}

MISSING_DEPS=0
check_cmd docker
check_cmd "docker compose"
check_cmd node
check_cmd npm
check_cmd aws
check_cmd terraform

if [ "${MISSING_DEPS}" -eq 1 ]; then
    error "Please install the missing tools above and re-run this script."
    exit 1
fi

# ─── Dashboard deps ────────────────────────────────────────────
heading "Installing Node.js dependencies"
cd "${REPO_ROOT}/dashboard"
if [ ! -d "node_modules" ]; then
    npm install
    info "Dependencies installed"
else
    info "node_modules already present — skipping"
fi
cd "${REPO_ROOT}"

# ─── Environment file ──────────────────────────────────────────
heading "Setting up environment files"
if [ ! -f "${REPO_ROOT}/dashboard/.env" ]; then
    cp "${REPO_ROOT}/dashboard/.env.example" "${REPO_ROOT}/dashboard/.env"
    info "Created dashboard/.env from .env.example"
    warn "Review dashboard/.env and update any values you need"
else
    info "dashboard/.env already exists — skipping"
fi

# ─── Terraform vars ────────────────────────────────────────────
heading "Setting up Terraform vars"
for env in dev prod; do
    EXAMPLE="${REPO_ROOT}/terraform/envs/${env}/terraform.tfvars.example"
    TARGET="${REPO_ROOT}/terraform/envs/${env}/terraform.tfvars"
    if [ -f "${EXAMPLE}" ] && [ ! -f "${TARGET}" ]; then
        cp "${EXAMPLE}" "${TARGET}"
        info "Created terraform/envs/${env}/terraform.tfvars"
        warn "Edit terraform/envs/${env}/terraform.tfvars with your settings before deploying"
    fi
done

# ─── LocalStack init script permissions ────────────────────────
chmod +x "${REPO_ROOT}/scripts/localstack-init.sh"
chmod +x "${REPO_ROOT}/server/RealmForgeServer.sh"
chmod +x "${REPO_ROOT}/server/install-gamelift-sdk.sh"
chmod +x "${REPO_ROOT}/scripts/deploy.sh"
chmod +x "${REPO_ROOT}/scripts/rollback.sh"

# ─── Pull Docker images ────────────────────────────────────────
heading "Pulling Docker images"
docker pull postgres:15-alpine
docker pull redis:7-alpine
docker pull localstack/localstack:3.0
docker pull nginx:alpine
info "Docker images pulled"

# ─── Start services ────────────────────────────────────────────
heading "Starting local services"
cd "${REPO_ROOT}"
docker compose up -d

info "Waiting for services to be ready..."
for i in $(seq 1 30); do
    if curl -sf http://localhost:3000/health &>/dev/null; then
        break
    fi
    sleep 2
done

# ─── Verify ────────────────────────────────────────────────────
heading "Verifying setup"

HEALTH=$(curl -sf http://localhost:3000/health 2>/dev/null || echo "")
if echo "${HEALTH}" | grep -q '"status":"ok"'; then
    info "✅ Dashboard API healthy at http://localhost:3000"
else
    warn "⚠️  Dashboard not responding yet — run: docker compose logs dashboard"
fi

if curl -sf http://localhost:4566/_localstack/health &>/dev/null; then
    info "✅ LocalStack running at http://localhost:4566"
fi

if psql "postgresql://realmforge:devpass@localhost:5432/realmforge" -c '\q' &>/dev/null; then
    info "✅ PostgreSQL ready at localhost:5432"
fi

# ─── Summary ───────────────────────────────────────────────────
heading "Setup Complete"
cat << 'SUMMARY'
┌─────────────────────────────────────────────────────┐
│  RealmForge Local Dev Environment                   │
├─────────────────────────────────────────────────────┤
│  Dashboard API    http://localhost:3000              │
│  WebSocket        ws://localhost:3000/ws             │
│  API Health       http://localhost:3000/health       │
│  Nginx proxy      http://localhost:80                │
│  LocalStack       http://localhost:4566              │
│  PostgreSQL       localhost:5432                     │
│  Redis            localhost:6379                     │
│                                                     │
│  Debug tools (docker compose --profile debug up):  │
│    Adminer DB UI  http://localhost:8080              │
│    Redis Cmd      http://localhost:8081              │
│                                                     │
│  Next steps:                                        │
│    1. Edit dashboard/.env if needed                 │
│    2. POST http://localhost:3000/api/auth/register  │
│    3. See docs/DEPLOYMENT_RUNBOOK.md to go to AWS   │
└─────────────────────────────────────────────────────┘
SUMMARY
