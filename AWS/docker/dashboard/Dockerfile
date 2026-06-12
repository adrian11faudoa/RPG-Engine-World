# ─── Stage 1: deps ────────────────────────────────────────────
FROM node:20-alpine AS deps

WORKDIR /app
COPY package*.json ./
RUN npm ci --omit=dev && npm cache clean --force

# ─── Stage 2: production image ────────────────────────────────
FROM node:20-alpine AS runner

# Security: run as non-root user
RUN addgroup -g 1001 -S nodejs && adduser -S realmforge -u 1001

WORKDIR /app

# Copy production deps and source
COPY --from=deps --chown=realmforge:nodejs /app/node_modules ./node_modules
COPY --chown=realmforge:nodejs . .

USER realmforge

ENV NODE_ENV=production
ENV PORT=3000

EXPOSE 3000

# Health check for ECS
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
  CMD wget -qO- http://localhost:3000/health || exit 1

CMD ["node", "src/server.js"]
