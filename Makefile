# RealmForge Engine AWS — Makefile
# Usage: make <target>

.PHONY: help dev dev-down dev-logs tf-init tf-plan tf-apply tf-destroy \
        docker-build docker-push deploy-dashboard deploy-server \
        db-migrate test lint clean

AWS_REGION   ?= us-east-1
ENV          ?= prod
TF_VARS      ?= terraform/envs/$(ENV)/terraform.tfvars
ECR_URL      ?= $(shell cd terraform && terraform output -raw ecr_repository_url 2>/dev/null)
CLUSTER      ?= realmforge-$(ENV)-cluster
SERVICE      ?= realmforge-$(ENV)-dashboard-svc

# ─── Help ────────────────────────────────────────────────────────
help: ## Show this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} \
	/^[a-zA-Z_-]+:.*?##/ { printf "  \033[36m%-22s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

# ─── Local Development ───────────────────────────────────────────
dev: ## Start local dev stack (LocalStack + DB + Redis + Dashboard)
	docker compose up -d
	@echo "✅ Dev stack running"
	@echo "   Dashboard:  http://localhost:3000"
	@echo "   Health:     http://localhost:3000/health"
	@echo "   LocalStack: http://localhost:4566"

dev-debug: ## Start dev stack with Adminer + Redis Commander
	docker compose --profile debug up -d

dev-down: ## Stop local dev stack
	docker compose down

dev-logs: ## Tail dashboard logs
	docker compose logs -f dashboard

dev-reset: ## Wipe all local volumes and restart
	docker compose down -v
	docker compose up -d

# ─── Terraform ───────────────────────────────────────────────────
tf-init: ## Initialise Terraform (first time setup)
	cd terraform && terraform init

tf-plan: ## Preview infrastructure changes
	cd terraform && terraform plan -var-file=../$(TF_VARS)

tf-apply: ## Apply infrastructure changes
	cd terraform && terraform apply -var-file=../$(TF_VARS)

tf-apply-auto: ## Apply without confirmation prompt (CI use)
	cd terraform && terraform apply -auto-approve -var-file=../$(TF_VARS)

tf-destroy: ## Destroy all infrastructure (DESTRUCTIVE)
	@echo "⚠️  This will DESTROY all AWS resources. Type 'yes' to continue:"
	@read CONFIRM && [ "$$CONFIRM" = "yes" ] || exit 1
	cd terraform && terraform destroy -var-file=../$(TF_VARS)

tf-output: ## Show Terraform outputs
	cd terraform && terraform output

tf-fmt: ## Format all Terraform files
	cd terraform && terraform fmt -recursive

tf-validate: ## Validate Terraform config
	cd terraform && terraform validate

# ─── Docker ──────────────────────────────────────────────────────
docker-build: ## Build dashboard Docker image
	docker build -f docker/dashboard/Dockerfile -t realmforge-dashboard:latest ./dashboard

docker-push: ## Push dashboard image to ECR
	@[ -n "$(ECR_URL)" ] || (echo "❌ ECR_URL not set. Run: make tf-output" && exit 1)
	aws ecr get-login-password --region $(AWS_REGION) | \
	  docker login --username AWS --password-stdin $(ECR_URL)
	docker tag realmforge-dashboard:latest $(ECR_URL):latest
	docker tag realmforge-dashboard:latest $(ECR_URL):$(shell git rev-parse --short HEAD)
	docker push $(ECR_URL):latest
	docker push $(ECR_URL):$(shell git rev-parse --short HEAD)
	@echo "✅ Pushed $(ECR_URL):latest"

docker-build-push: docker-build docker-push ## Build and push in one step

# ─── Deployments ─────────────────────────────────────────────────
deploy-dashboard: docker-build-push ## Build, push, and deploy dashboard to ECS
	aws ecs update-service \
	  --cluster $(CLUSTER) \
	  --service $(SERVICE) \
	  --force-new-deployment \
	  --region $(AWS_REGION) \
	  --output text --query 'service.serviceName'
	@echo "⏳ Waiting for deployment to stabilize..."
	aws ecs wait services-stable \
	  --cluster $(CLUSTER) \
	  --services $(SERVICE) \
	  --region $(AWS_REGION)
	@echo "✅ Dashboard deployed"

rollback-dashboard: ## Roll back ECS dashboard to previous task definition
	@PREV=$$(aws ecs describe-task-definition \
	  --task-definition realmforge-$(ENV)-dashboard \
	  --query 'taskDefinition.revision' --output text); \
	PREV=$$((PREV - 1)); \
	aws ecs update-service \
	  --cluster $(CLUSTER) \
	  --service $(SERVICE) \
	  --task-definition realmforge-$(ENV)-dashboard:$$PREV \
	  --region $(AWS_REGION)
	@echo "⏪ Rolled back to previous revision"

deploy-server: ## Package and upload server build to GameLift
	bash scripts/build-server.sh
	bash scripts/deploy-server.sh

invalidate-cdn: ## Invalidate CloudFront cache
	@DIST_ID=$$(aws cloudfront list-distributions \
	  --query "DistributionList.Items[?Comment=='RealmForge Engine CDN'].Id" \
	  --output text --region $(AWS_REGION)); \
	aws cloudfront create-invalidation \
	  --distribution-id $$DIST_ID \
	  --paths "/*" \
	  --region $(AWS_REGION)
	@echo "✅ CloudFront cache invalidated"

# ─── Database ────────────────────────────────────────────────────
db-migrate: ## Run DB schema migrations (local)
	docker compose exec postgres psql -U realmforge -d realmforge -f /docker-entrypoint-initdb.d/init.sql
	@echo "✅ Migration complete"

db-shell: ## Open psql shell (local)
	docker compose exec postgres psql -U realmforge -d realmforge

db-dump: ## Dump local DB to file
	docker compose exec postgres pg_dump -U realmforge realmforge > backups/db-$(shell date +%Y%m%d-%H%M%S).sql
	@echo "✅ Dump saved to backups/"

# ─── Tests ───────────────────────────────────────────────────────
test: ## Run dashboard tests
	cd dashboard && npm test

test-watch: ## Run tests in watch mode
	cd dashboard && npm test -- --watch

lint: ## Lint dashboard JS
	cd dashboard && npx eslint src/ --ext .js

# ─── Logs & Monitoring ───────────────────────────────────────────
logs-api: ## Tail ECS dashboard logs (prod)
	aws logs tail /ecs/realmforge-$(ENV)-dashboard --follow --region $(AWS_REGION)

logs-rds: ## Tail RDS PostgreSQL logs
	aws rds download-db-log-file-portion \
	  --db-instance-identifier realmforge-$(ENV)-postgres \
	  --log-file-name error/postgresql.log \
	  --region $(AWS_REGION)

status: ## Show ECS service status
	aws ecs describe-services \
	  --cluster $(CLUSTER) \
	  --services $(SERVICE) \
	  --region $(AWS_REGION) \
	  --query 'services[0].{Status:status,Running:runningCount,Desired:desiredCount,Events:events[:3]}'

gamelift-status: ## Show GameLift fleet status
	@FLEET=$$(cd terraform && terraform output -raw game_server_fleet_id 2>/dev/null); \
	aws gamelift describe-fleet-attributes --fleet-ids $$FLEET --region $(AWS_REGION) \
	  --query 'FleetAttributes[0].{Status:Status,Type:EC2InstanceType}'

# ─── Setup ───────────────────────────────────────────────────────
setup: ## First-time setup: install tools check
	@echo "Checking prerequisites..."
	@command -v terraform >/dev/null 2>&1 && echo "✅ terraform" || echo "❌ terraform (brew install terraform)"
	@command -v aws >/dev/null 2>&1 && echo "✅ aws cli" || echo "❌ aws cli (brew install awscli)"
	@command -v docker >/dev/null 2>&1 && echo "✅ docker" || echo "❌ docker (https://docker.com)"
	@command -v node >/dev/null 2>&1 && echo "✅ node $$(node -v)" || echo "❌ node (brew install node)"
	@aws sts get-caller-identity --query 'Account' --output text 2>/dev/null \
	  && echo "✅ AWS credentials" || echo "❌ AWS credentials (aws configure)"

install: ## Install dashboard npm dependencies
	cd dashboard && npm install

clean: ## Remove local build artifacts
	rm -rf dashboard/node_modules
	rm -rf server/build/
	rm -f *.zip
	docker compose down -v 2>/dev/null || true
