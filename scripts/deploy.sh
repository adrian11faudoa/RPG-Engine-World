#!/bin/bash
##############################################################
# scripts/deploy.sh
# Convenience wrapper for deploying RealmForge to AWS.
# Usage:
#   ./scripts/deploy.sh [env]         # deploy dashboard only
#   ./scripts/deploy.sh [env] --server # also deploy game server
#
# env: prod (default) | dev
##############################################################

set -euo pipefail

ENV="${1:-prod}"
DEPLOY_SERVER="${2:-}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TF_DIR="${REPO_ROOT}/terraform"
DASHBOARD_DIR="${REPO_ROOT}/dashboard"

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[deploy:${ENV}]${NC} $*"; }
warn()    { echo -e "${YELLOW}[deploy:${ENV}]${NC} $*"; }
step()    { echo -e "\n${GREEN}══ $* ══${NC}"; }

# ─── Validate ──────────────────────────────────────────────────
if [ "${ENV}" != "prod" ] && [ "${ENV}" != "dev" ]; then
    echo "Usage: $0 [prod|dev] [--server]"; exit 1
fi

if [ ! -f "${TF_DIR}/envs/${ENV}/terraform.tfvars" ]; then
    echo "ERROR: terraform/envs/${ENV}/terraform.tfvars not found"
    echo "Copy from .example and fill in values first."
    exit 1
fi

VARS_FILE="${TF_DIR}/envs/${ENV}/terraform.tfvars"

step "1/5 Terraform plan"
cd "${TF_DIR}"
terraform init -backend-config="envs/${ENV}/backend.tf" -reconfigure
terraform plan -var-file="${VARS_FILE}" -out="${ENV}.plan"

read -p "Apply infrastructure changes? (y/N) " -n 1 -r
echo
if [[ ! "${REPLY}" =~ ^[Yy]$ ]]; then
    warn "Skipping Terraform apply."
else
    step "2/5 Terraform apply"
    terraform apply "${ENV}.plan"
    rm -f "${ENV}.plan"
fi

# ─── Get outputs ───────────────────────────────────────────────
ECR_URL=$(terraform output -raw ecr_repository_url 2>/dev/null || echo "")
ECS_CLUSTER=$(terraform output -raw cluster_name   2>/dev/null || echo "realmforge-${ENV}-cluster")
ECS_SERVICE=$(terraform output -raw service_name   2>/dev/null || echo "realmforge-${ENV}-dashboard-svc")
CF_ID=$(terraform output -raw cloudfront_id        2>/dev/null || echo "")

if [ -z "${ECR_URL}" ]; then
    warn "Could not get ECR URL from Terraform outputs. Skipping Docker build."
else
    # ─── Docker build & push ─────────────────────────────────────
    step "3/5 Build & push Docker image"

    AWS_REGION="${AWS_DEFAULT_REGION:-us-east-1}"
    AWS_ACCOUNT=$(aws sts get-caller-identity --query Account --output text)
    ECR_REGISTRY="${AWS_ACCOUNT}.dkr.ecr.${AWS_REGION}.amazonaws.com"
    IMAGE_TAG="$(git rev-parse --short HEAD 2>/dev/null || echo latest)"

    aws ecr get-login-password --region "${AWS_REGION}" | \
        docker login --username AWS --password-stdin "${ECR_REGISTRY}"

    docker build \
        -f "${REPO_ROOT}/docker/dashboard/Dockerfile" \
        -t "${ECR_URL}:${IMAGE_TAG}" \
        -t "${ECR_URL}:latest" \
        --cache-from "${ECR_URL}:latest" \
        "${DASHBOARD_DIR}"

    docker push "${ECR_URL}:${IMAGE_TAG}"
    docker push "${ECR_URL}:latest"
    info "Pushed: ${ECR_URL}:${IMAGE_TAG}"

    # ─── Update ECS ──────────────────────────────────────────────
    step "4/5 Deploy to ECS"

    aws ecs update-service \
        --cluster "${ECS_CLUSTER}" \
        --service  "${ECS_SERVICE}" \
        --force-new-deployment \
        --region "${AWS_REGION}" > /dev/null

    info "Waiting for service to stabilize..."
    aws ecs wait services-stable \
        --cluster "${ECS_CLUSTER}" \
        --services "${ECS_SERVICE}" \
        --region "${AWS_REGION}"

    info "ECS deployment complete"

    # ─── Invalidate CloudFront ────────────────────────────────────
    if [ -n "${CF_ID}" ]; then
        step "5/5 Invalidate CloudFront cache"
        aws cloudfront create-invalidation \
            --distribution-id "${CF_ID}" \
            --paths "/api/*" "/" > /dev/null
        info "CloudFront cache invalidated"
    fi
fi

# ─── Optional: deploy game server ──────────────────────────────
if [ "${DEPLOY_SERVER}" = "--server" ]; then
    step "Deploying game server to GameLift"
    BUILD_BUCKET=$(terraform output -raw build_bucket 2>/dev/null || echo "")
    BUILD_ROLE=$(aws iam get-role \
        --role-name "realmforge-${ENV}-gamelift-build-role" \
        --query 'Role.Arn' --output text 2>/dev/null || echo "")

    SERVER_ZIP="${REPO_ROOT}/server/build/RealmForgeServer-Linux.zip"
    if [ ! -f "${SERVER_ZIP}" ]; then
        warn "Server zip not found at ${SERVER_ZIP}. Build UE5 server first."
    else
        aws s3 cp "${SERVER_ZIP}" "s3://${BUILD_BUCKET}/server-builds/RealmForgeServer-Linux.zip"
        info "Server build uploaded to S3"

        BUILD_ID=$(aws gamelift create-build \
            --name "RealmForge-$(git rev-parse --short HEAD 2>/dev/null || echo manual)" \
            --operating-system AMAZON_LINUX_2 \
            --storage-location "Bucket=${BUILD_BUCKET},Key=server-builds/RealmForgeServer-Linux.zip,RoleArn=${BUILD_ROLE}" \
            --query 'Build.BuildId' --output text)

        info "GameLift build created: ${BUILD_ID}"
        info "Update the fleet manually in the AWS Console or via Terraform to use this build."
    fi
fi

step "Deployment complete ✅"
DOMAIN=$(grep domain_name "${VARS_FILE}" | sed "s/.*= *\"//;s/\".*//")
info "Dashboard: https://${DOMAIN}"
