#!/bin/bash
##############################################################
# RealmForge GameLift Server Launch Script
# Called by GameLift fleet runtime configuration
# Wraps the UE5 dedicated server with proper env setup
##############################################################

set -euo pipefail

# ─── Environment ───────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_BIN="${SCRIPT_DIR}/RealmForgeServer"
LOG_DIR="/local/game/logs"
PORT="${1:-7777}"

mkdir -p "${LOG_DIR}"

LOG_FILE="${LOG_DIR}/server-$(date +%Y%m%d-%H%M%S).log"

echo "[RealmForge] Starting server on port ${PORT}" | tee -a "${LOG_FILE}"
echo "[RealmForge] Script dir: ${SCRIPT_DIR}" | tee -a "${LOG_FILE}"
echo "[RealmForge] Time: $(date -u)" | tee -a "${LOG_FILE}"

# ─── AWS Credentials via IAM Role (no hardcoded keys) ──────────
# GameLift instance role provides credentials automatically

# ─── Fetch config from SSM Parameter Store ────────────────────
if command -v aws &> /dev/null; then
  AWS_REGION="${AWS_DEFAULT_REGION:-us-east-1}"

  DB_URL=$(aws ssm get-parameter \
    --name "/realmforge/prod/db-url" \
    --with-decryption \
    --region "${AWS_REGION}" \
    --query 'Parameter.Value' \
    --output text 2>/dev/null || echo "")

  REDIS_URL=$(aws ssm get-parameter \
    --name "/realmforge/prod/redis-url" \
    --with-decryption \
    --region "${AWS_REGION}" \
    --query 'Parameter.Value' \
    --output text 2>/dev/null || echo "")

  export RF_DB_URL="${DB_URL}"
  export RF_REDIS_URL="${REDIS_URL}"
fi

# ─── Verify binary exists ──────────────────────────────────────
if [ ! -f "${SERVER_BIN}" ]; then
  echo "[ERROR] Server binary not found: ${SERVER_BIN}" | tee -a "${LOG_FILE}"
  exit 1
fi

chmod +x "${SERVER_BIN}"

# ─── Launch UE5 Dedicated Server ──────────────────────────────
exec "${SERVER_BIN}" \
  -log \
  -port="${PORT}" \
  -MaxPlayers=8 \
  -nosplash \
  -nullrhi \
  -noverifygc \
  -gamelift \
  -aws_region="${AWS_REGION:-us-east-1}" \
  >> "${LOG_FILE}" 2>&1
