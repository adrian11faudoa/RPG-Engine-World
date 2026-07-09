#!/bin/bash
##############################################################
# scripts/rollback.sh
# Rolls back the dashboard to a previous ECS task definition
# or the GameLift alias to a previous fleet.
#
# Usage:
#   ./scripts/rollback.sh [env]                  # rollback ECS by 1
#   ./scripts/rollback.sh [env] --steps 2        # rollback 2 revisions
#   ./scripts/rollback.sh [env] --fleet fleet-xx # swap GameLift alias
##############################################################

set -euo pipefail

ENV="${1:-prod}"
STEPS=1
FLEET_ID=""

# Parse extra args
shift
while [[ $# -gt 0 ]]; do
    case "$1" in
        --steps)  STEPS="$2";   shift 2 ;;
        --fleet)  FLEET_ID="$2"; shift 2 ;;
        *) shift ;;
    esac
done

AWS_REGION="${AWS_DEFAULT_REGION:-us-east-1}"
TF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/terraform"

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
info()  { echo -e "${GREEN}[rollback:${ENV}]${NC} $*"; }
error() { echo -e "${RED}[rollback:${ENV}]${NC} $*" >&2; }

# ─── ECS Rollback ──────────────────────────────────────────────
if [ -z "${FLEET_ID}" ]; then
    cd "${TF_DIR}"
    CLUSTER=$(terraform output -raw cluster_name 2>/dev/null || echo "realmforge-${ENV}-cluster")
    SERVICE=$(terraform output -raw service_name 2>/dev/null || echo "realmforge-${ENV}-dashboard-svc")
    FAMILY="realmforge-${ENV}-dashboard"

    info "Rolling back ECS service ${SERVICE} by ${STEPS} revision(s)..."

    # Get current revision
    CURRENT_ARN=$(aws ecs describe-services \
        --cluster "${CLUSTER}" --services "${SERVICE}" \
        --query 'services[0].taskDefinition' --output text --region "${AWS_REGION}")
    CURRENT_REV=$(echo "${CURRENT_ARN}" | grep -oE '[0-9]+$')
    TARGET_REV=$((CURRENT_REV - STEPS))

    if [ "${TARGET_REV}" -lt 1 ]; then
        error "Cannot roll back: would go below revision 1 (current: ${CURRENT_REV}, steps: ${STEPS})"
        exit 1
    fi

    TARGET_ARN="arn:aws:ecs:${AWS_REGION}:$(aws sts get-caller-identity --query Account --output text):task-definition/${FAMILY}:${TARGET_REV}"

    info "Current revision: ${CURRENT_REV}"
    info "Rolling back to:  ${TARGET_REV} (${TARGET_ARN})"
    read -p "Confirm rollback? (y/N) " -n 1 -r; echo
    [[ ! "${REPLY}" =~ ^[Yy]$ ]] && { info "Cancelled."; exit 0; }

    aws ecs update-service \
        --cluster "${CLUSTER}" \
        --service  "${SERVICE}" \
        --task-definition "${TARGET_ARN}" \
        --region "${AWS_REGION}" > /dev/null

    info "Waiting for rollback to stabilize..."
    aws ecs wait services-stable \
        --cluster "${CLUSTER}" \
        --services "${SERVICE}" \
        --region "${AWS_REGION}"

    info "✅ ECS rolled back to revision ${TARGET_REV}"
fi

# ─── GameLift Alias Swap ───────────────────────────────────────
if [ -n "${FLEET_ID}" ]; then
    cd "${TF_DIR}"
    ALIAS_ID=$(terraform output -raw alias_id 2>/dev/null || echo "")

    if [ -z "${ALIAS_ID}" ]; then
        error "Could not get alias_id from Terraform outputs"
        exit 1
    fi

    info "Switching GameLift alias ${ALIAS_ID} → fleet ${FLEET_ID}"
    read -p "Confirm fleet swap? (y/N) " -n 1 -r; echo
    [[ ! "${REPLY}" =~ ^[Yy]$ ]] && { info "Cancelled."; exit 0; }

    aws gamelift update-alias \
        --alias-id "${ALIAS_ID}" \
        --routing-strategy "Type=SIMPLE,FleetId=${FLEET_ID}" \
        --region "${AWS_REGION}" > /dev/null

    info "✅ GameLift alias now points to fleet ${FLEET_ID}"
    info "New sessions will be routed to the rollback fleet immediately."
    info "Existing sessions on the old fleet will finish normally."
fi
