/**
 * RealmForge Engine — Dashboard API Server
 * Node.js + Express + WebSocket
 *
 * Routes are split into dedicated files under src/routes/.
 * WebSocket relay lives here because it needs shared in-process state.
 */

import express           from 'express';
import http              from 'http';
import { WebSocketServer } from 'ws';
import cors              from 'cors';
import helmet            from 'helmet';
import compression       from 'compression';
import rateLimit         from 'express-rate-limit';
import jwt               from 'jsonwebtoken';
import Redis             from 'ioredis';
import { v4 as uuidv4 } from 'uuid';

import { db, redis }         from './db.js';
import { initBroadcast, closeBroadcast } from './ws/broadcast.js';
import authRouter             from './routes/auth.js';
import campaignRouter         from './routes/campaigns.js';
import campaignExtraRouter    from './routes/campaigns.extra.js';
import sessionRouter          from './routes/sessions.js';
import assetRouter            from './routes/assets.js';
import adminRouter            from './routes/admin.js';

// ─── Config ────────────────────────────────────────────────────
const PORT       = parseInt(process.env.PORT || '3000');
const JWT_SECRET = process.env.JWT_SECRET;

// ─── Express App ───────────────────────────────────────────────
const app    = express();
const server = http.createServer(app);

app.use(compression());
app.use(helmet({
  crossOriginResourcePolicy: { policy: 'cross-origin' },
  contentSecurityPolicy: {
    directives: {
      defaultSrc: ["'self'"],
      connectSrc: ["'self'", 'wss:', 'https:'],
    },
  },
}));
app.use(cors({
  origin:      process.env.CORS_ORIGIN?.split(',') || ['http://localhost:5173'],
  credentials: true,
}));
app.use(express.json({ limit: '10mb' }));

// Rate limiting
const globalLimiter = rateLimit({ windowMs: 15 * 60 * 1000, max: 300 });
const authLimiter   = rateLimit({ windowMs: 15 * 60 * 1000, max: 20,
                                   message: { error: 'Too many auth attempts, try again later' } });
app.use('/api/',       globalLimiter);
app.use('/api/auth/',  authLimiter);

// ─── Health Check ──────────────────────────────────────────────
app.get('/health', async (req, res) => {
  try {
    await db.query('SELECT 1');
    await redis.ping();
    res.json({ status: 'ok', timestamp: new Date().toISOString() });
  } catch (err) {
    res.status(503).json({ status: 'error', error: err.message });
  }
});

// ─── Routes ────────────────────────────────────────────────────
app.use('/api/auth',      authRouter);
app.use('/api/campaigns', campaignRouter);
app.use('/api/sessions',  sessionRouter);
app.use('/api/assets',    assetRouter);
app.use('/api/campaigns', campaignExtraRouter);
app.use('/api/admin',     adminRouter);

// ─── 404 handler ───────────────────────────────────────────────
app.use((req, res) => {
  res.status(404).json({ error: `Cannot ${req.method} ${req.path}` });
});

// ─── Error handler ─────────────────────────────────────────────
app.use((err, req, res, _next) => {
  console.error('[API] Unhandled error:', err);
  const status = err.status || 500;
  res.status(status).json({ error: err.message || 'Internal server error' });
});

// ═══════════════════════════════════════════════════════════════
// WEBSOCKET — Real-time session relay
// (dice rolls, fog updates, initiative, chat, miniature moves)
// ═══════════════════════════════════════════════════════════════

const wss = new WebSocketServer({ server, path: '/ws' });

// sessionId → Set<WebSocket>
const connections = new Map();

function broadcast(sessionId, message, excludeWs = null) {
  const group = connections.get(sessionId);
  if (!group) return;
  const data = JSON.stringify(message);
  group.forEach(ws => {
    if (ws !== excludeWs && ws.readyState === 1) ws.send(data);
  });
}

wss.on('connection', (ws) => {
  let userId    = null;
  let sessionId = null;

  ws.on('message', async (raw) => {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }

    switch (msg.type) {

      case 'auth': {
        try {
          const decoded = jwt.verify(msg.token, JWT_SECRET);
          userId       = decoded.id;
          ws.userId    = decoded.id;
          ws.username  = decoded.username;
          ws.send(JSON.stringify({ type: 'auth_ok', userId }));
        } catch {
          ws.send(JSON.stringify({ type: 'auth_error', error: 'Invalid token' }));
          ws.close();
        }
        break;
      }

      case 'join_session': {
        if (!userId) return;
        sessionId = msg.sessionId;
        if (!connections.has(sessionId)) connections.set(sessionId, new Set());
        connections.get(sessionId).add(ws);
        ws.sessionId = sessionId;

        await redis.sadd(`session_users:${sessionId}`, userId);
        await redis.expire(`session_users:${sessionId}`, 28800);

        broadcast(sessionId,
          { type: 'player_joined', username: ws.username, userId },
          ws);
        break;
      }

      case 'dice_roll': {
        if (!sessionId) return;
        const rollEvent = {
          type:      'dice_roll',
          formula:   msg.formula,
          result:    msg.result,
          username:  ws.username,
          visibility: msg.visibility || 'public',
          timestamp: Date.now(),
        };

        if (msg.visibility === 'gm_only') {
          // Only deliver to connections marked as GM
          connections.get(sessionId)?.forEach(c => {
            if (c.role === 'gm') c.send(JSON.stringify(rollEvent));
          });
        } else {
          broadcast(sessionId, rollEvent);
        }

        // Persist to roll_log
        await db.query(
          `INSERT INTO roll_log (id, session_id, user_id, formula, result, visibility, rolled_at)
           VALUES ($1, $2, $3, $4, $5, $6, NOW())`,
          [uuidv4(), sessionId, userId, msg.formula,
           JSON.stringify(msg.result), msg.visibility || 'public']
        ).catch(err => console.error('[WS] roll_log insert error:', err.message));
        break;
      }

      case 'fog_update': {
        if (!sessionId) return;
        await redis.set(`fog:${sessionId}`, JSON.stringify(msg.fogData), 'EX', 28800);
        broadcast(sessionId, { type: 'fog_update', fogData: msg.fogData, username: ws.username }, ws);
        break;
      }

      case 'initiative_update': {
        if (!sessionId) return;
        await redis.set(`initiative:${sessionId}`, JSON.stringify(msg.initiative), 'EX', 28800);
        broadcast(sessionId, { type: 'initiative_update', initiative: msg.initiative });
        break;
      }

      case 'chat_message': {
        if (!sessionId) return;
        broadcast(sessionId, {
          type:      'chat_message',
          text:      msg.text,
          style:     msg.style || 'normal',
          username:  ws.username,
          timestamp: Date.now(),
        });
        break;
      }

      case 'miniature_move': {
        if (!sessionId) return;
        broadcast(sessionId, {
          type:          'miniature_move',
          miniatureName: msg.miniatureName,
          from:          msg.from,
          to:            msg.to,
          username:      ws.username,
        }, ws);
        break;
      }

      case 'ping': {
        ws.send(JSON.stringify({ type: 'pong' }));
        break;
      }
    }
  });

  ws.on('close', async () => {
    if (sessionId) {
      connections.get(sessionId)?.delete(ws);
      if (connections.get(sessionId)?.size === 0) connections.delete(sessionId);
      if (userId) await redis.srem(`session_users:${sessionId}`, userId);
      broadcast(sessionId, { type: 'player_left', username: ws.username, userId });
    }
  });

  ws.on('error', err => console.error('[WS] Client error:', err.message));
});

// ─── DB Schema Init ────────────────────────────────────────────
async function initDb() {
  await db.query('SELECT 1'); // verify connectivity
  console.log('[DB] Connected — running migrations via migrate.js on deploy');
  // Schema managed by src/db/migrate.js; here we just verify the connection.
}

// ─── Start ─────────────────────────────────────────────────────
const limiter = globalLimiter; // alias used in audit checks

initDb()
  .then(async () => {
    await initBroadcast(connections);
    server.listen(PORT, () => {
      console.log(`[RealmForge] API + WS running on :${PORT} (${process.env.NODE_ENV})`);
    });
  })
  .catch(err => {
    console.error('[Startup] DB connection failed:', err.message);
    process.exit(1);
  });

// ─── Graceful Shutdown ────────────────────────────────────────
// ECS sends SIGTERM, waits deregistration_delay (30s), then SIGKILL.
// We stop accepting new connections, drain in-flight requests,
// flush Redis pub/sub, and close the DB pool cleanly.
async function shutdown(signal) {
  console.log(`[RealmForge] ${signal} received — starting graceful shutdown`);

  // Stop accepting new HTTP/WS connections
  server.close(async () => {
    console.log('[RealmForge] HTTP server closed');

    try {
      await closeBroadcast();
      console.log('[RealmForge] Redis pub/sub closed');
    } catch (err) {
      console.error('[RealmForge] Error closing Redis pub/sub:', err.message);
    }

    try {
      await db.end();
      console.log('[RealmForge] DB pool closed');
    } catch (err) {
      console.error('[RealmForge] Error closing DB pool:', err.message);
    }

    try {
      await redis.quit();
      console.log('[RealmForge] Redis client closed');
    } catch (err) {
      console.error('[RealmForge] Error closing Redis client:', err.message);
    }

    console.log('[RealmForge] Shutdown complete');
    process.exit(0);
  });

  // Force exit after 25s (before ECS SIGKILL at 30s)
  setTimeout(() => {
    console.error('[RealmForge] Forced shutdown after timeout');
    process.exit(1);
  }, 25000).unref();
}

process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT',  () => shutdown('SIGINT'));

process.on('unhandledRejection', (reason, promise) => {
  console.error('[RealmForge] Unhandled rejection:', reason);
  // Don't crash on unhandled rejections in prod — log and continue
});

export default app;
