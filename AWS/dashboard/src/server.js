/**
 * RealmForge Engine — Dashboard API Server
 * Node.js + Express + WebSocket
 * Handles: auth, campaigns, sessions, GameLift orchestration, asset uploads
 */

import express        from 'express';
import http           from 'http';
import { WebSocketServer } from 'ws';
import cors           from 'cors';
import helmet         from 'helmet';
import compression    from 'compression';
import rateLimit      from 'express-rate-limit';
import { Pool }       from 'pg';
import Redis          from 'ioredis';
import { S3Client, PutObjectCommand, GetObjectCommand, DeleteObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import {
  GameLiftClient,
  CreateGameSessionCommand,
  DescribeGameSessionsCommand,
  CreatePlayerSessionCommand,
  SearchGameSessionsCommand,
  ListFleetsCommand,
  DescribeFleetAttributesCommand,
} from '@aws-sdk/client-gamelift';
import jwt        from 'jsonwebtoken';
import bcrypt     from 'bcryptjs';
import { v4 as uuidv4 } from 'uuid';

// ─── Config ────────────────────────────────────────────────────
const PORT         = parseInt(process.env.PORT || '3000');
const DATABASE_URL = process.env.DATABASE_URL;
const REDIS_URL    = process.env.REDIS_URL;
const JWT_SECRET   = process.env.JWT_SECRET;
const AWS_REGION   = process.env.AWS_REGION || 'us-east-1';
const ASSETS_BUCKET = process.env.ASSETS_BUCKET;
const GAMELIFT_QUEUE = process.env.GAMELIFT_QUEUE || 'realmforge-prod-queue';
const GAMELIFT_FLEET = process.env.GAMELIFT_FLEET_ID;

// ─── AWS Clients ───────────────────────────────────────────────
const s3       = new S3Client({ region: AWS_REGION });
const gamelift = new GameLiftClient({ region: AWS_REGION });

// ─── Database ──────────────────────────────────────────────────
const db = new Pool({ connectionString: DATABASE_URL, max: 20 });

// ─── Redis ─────────────────────────────────────────────────────
const redis = new Redis(REDIS_URL, {
  retryStrategy: (times) => Math.min(times * 100, 3000),
  maxRetriesPerRequest: 3,
  enableReadyCheck: true,
});

redis.on('error', (err) => console.error('[Redis] Error:', err));

// ─── Express App ───────────────────────────────────────────────
const app = express();
const server = http.createServer(app);

app.use(compression());
app.use(helmet({
  crossOriginResourcePolicy: { policy: 'cross-origin' },
  contentSecurityPolicy: {
    directives: {
      defaultSrc: ["'self'"],
      connectSrc: ["'self'", 'wss:', 'https:'],
      imgSrc:     ["'self'", 'data:', `https://${ASSETS_BUCKET}.s3.amazonaws.com`],
    }
  }
}));
app.use(cors({
  origin: process.env.CORS_ORIGIN?.split(',') || ['http://localhost:5173'],
  credentials: true,
}));
app.use(express.json({ limit: '10mb' }));

const limiter = rateLimit({ windowMs: 15 * 60 * 1000, max: 300 });
const authLimiter = rateLimit({ windowMs: 15 * 60 * 1000, max: 20, message: 'Too many auth attempts' });
app.use('/api/', limiter);
app.use('/api/auth/', authLimiter);

// ─── JWT Middleware ────────────────────────────────────────────
function authMiddleware(req, res, next) {
  const token = req.headers.authorization?.replace('Bearer ', '');
  if (!token) return res.status(401).json({ error: 'No token' });

  try {
    req.user = jwt.verify(token, JWT_SECRET);
    next();
  } catch {
    return res.status(401).json({ error: 'Invalid token' });
  }
}

function gmMiddleware(req, res, next) {
  authMiddleware(req, res, () => {
    if (req.user.role !== 'gm' && req.user.role !== 'admin') {
      return res.status(403).json({ error: 'GM role required' });
    }
    next();
  });
}

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

// ═══════════════════════════════════════════════════════════════
// AUTH ROUTES
// ═══════════════════════════════════════════════════════════════

app.post('/api/auth/register', async (req, res) => {
  const { username, email, password, inviteCode } = req.body;
  if (!username || !email || !password) {
    return res.status(400).json({ error: 'Missing fields' });
  }

  try {
    const hashedPassword = await bcrypt.hash(password, 12);
    const { rows } = await db.query(
      `INSERT INTO users (id, username, email, password_hash, role, created_at)
       VALUES ($1, $2, $3, $4, 'player', NOW())
       RETURNING id, username, email, role`,
      [uuidv4(), username, email, hashedPassword]
    );
    const user = rows[0];
    const token = jwt.sign({ id: user.id, username: user.username, role: user.role }, JWT_SECRET, { expiresIn: '7d' });
    res.status(201).json({ token, user });
  } catch (err) {
    if (err.code === '23505') return res.status(409).json({ error: 'Username or email taken' });
    throw err;
  }
});

app.post('/api/auth/login', async (req, res) => {
  const { email, password } = req.body;
  const { rows } = await db.query('SELECT * FROM users WHERE email = $1', [email]);
  const user = rows[0];

  if (!user || !(await bcrypt.compare(password, user.password_hash))) {
    return res.status(401).json({ error: 'Invalid credentials' });
  }

  await db.query('UPDATE users SET last_login = NOW() WHERE id = $1', [user.id]);

  const token = jwt.sign({ id: user.id, username: user.username, role: user.role }, JWT_SECRET, { expiresIn: '7d' });
  res.json({ token, user: { id: user.id, username: user.username, email: user.email, role: user.role } });
});

app.get('/api/auth/me', authMiddleware, async (req, res) => {
  const { rows } = await db.query(
    'SELECT id, username, email, role, created_at, last_login FROM users WHERE id = $1',
    [req.user.id]
  );
  res.json(rows[0]);
});

// ═══════════════════════════════════════════════════════════════
// CAMPAIGN ROUTES
// ═══════════════════════════════════════════════════════════════

app.get('/api/campaigns', authMiddleware, async (req, res) => {
  const { rows } = await db.query(
    `SELECT c.*, u.username as gm_name,
            COUNT(DISTINCT cs.user_id) as player_count
     FROM campaigns c
     JOIN users u ON c.gm_id = u.id
     LEFT JOIN campaign_sessions cs ON cs.campaign_id = c.id
     WHERE c.gm_id = $1 OR c.id IN (
       SELECT campaign_id FROM campaign_players WHERE user_id = $1
     )
     GROUP BY c.id, u.username
     ORDER BY c.updated_at DESC`,
    [req.user.id]
  );
  res.json(rows);
});

app.post('/api/campaigns', authMiddleware, async (req, res) => {
  const { title, description, setting, system } = req.body;
  const { rows } = await db.query(
    `INSERT INTO campaigns (id, title, description, setting, system, gm_id, created_at, updated_at)
     VALUES ($1, $2, $3, $4, $5, $6, NOW(), NOW())
     RETURNING *`,
    [uuidv4(), title, description, setting || 'Fantasy', system || 'D&D 5e', req.user.id]
  );
  res.status(201).json(rows[0]);
});

app.get('/api/campaigns/:id', authMiddleware, async (req, res) => {
  const { rows } = await db.query(
    `SELECT c.*, u.username as gm_name FROM campaigns c
     JOIN users u ON c.gm_id = u.id
     WHERE c.id = $1`,
    [req.params.id]
  );
  if (!rows[0]) return res.status(404).json({ error: 'Campaign not found' });
  res.json(rows[0]);
});

app.patch('/api/campaigns/:id', authMiddleware, gmMiddleware, async (req, res) => {
  const { title, description, world_state, active_map } = req.body;
  const { rows } = await db.query(
    `UPDATE campaigns SET title = COALESCE($1, title),
     description = COALESCE($2, description),
     world_state = COALESCE($3, world_state),
     active_map = COALESCE($4, active_map),
     updated_at = NOW()
     WHERE id = $5 RETURNING *`,
    [title, description, world_state, active_map, req.params.id]
  );
  res.json(rows[0]);
});

// Campaign journal entries
app.get('/api/campaigns/:id/journal', authMiddleware, async (req, res) => {
  const includeGM = req.user.role === 'gm';
  const { rows } = await db.query(
    `SELECT * FROM journal_entries WHERE campaign_id = $1
     ${!includeGM ? 'AND is_gm_only = false' : ''}
     ORDER BY created_at DESC`,
    [req.params.id]
  );
  res.json(rows);
});

app.post('/api/campaigns/:id/journal', authMiddleware, async (req, res) => {
  const { title, body, is_gm_only } = req.body;
  const { rows } = await db.query(
    `INSERT INTO journal_entries (id, campaign_id, title, body, author_id, is_gm_only, created_at)
     VALUES ($1, $2, $3, $4, $5, $6, NOW()) RETURNING *`,
    [uuidv4(), req.params.id, title, body, req.user.id, is_gm_only || false]
  );
  res.status(201).json(rows[0]);
});

// ═══════════════════════════════════════════════════════════════
// GAME SESSION ROUTES (GameLift)
// ═══════════════════════════════════════════════════════════════

app.post('/api/sessions/create', authMiddleware, async (req, res) => {
  const { campaignId, maxPlayers = 8, sessionName } = req.body;

  try {
    // Verify user owns this campaign
    const { rows: cam } = await db.query(
      'SELECT id FROM campaigns WHERE id = $1 AND gm_id = $2',
      [campaignId, req.user.id]
    );
    if (!cam[0]) return res.status(403).json({ error: 'You must be the GM to create a session' });

    // Create game session on GameLift
    const command = new CreateGameSessionCommand({
      FleetId: GAMELIFT_FLEET,
      MaximumPlayerSessionCount: maxPlayers,
      Name: sessionName || `${req.user.username}'s Session`,
      GameProperties: [
        { Key: 'CampaignId',   Value: campaignId },
        { Key: 'GMId',         Value: req.user.id },
        { Key: 'GMName',       Value: req.user.username },
      ],
    });

    const response = await gamelift.send(command);
    const gameSession = response.GameSession;

    // Save to DB
    await db.query(
      `INSERT INTO game_sessions (id, campaign_id, gamelift_session_id, status, gm_id, created_at)
       VALUES ($1, $2, $3, 'ACTIVE', $4, NOW())`,
      [uuidv4(), campaignId, gameSession.GameSessionId, req.user.id]
    );

    // Cache active session in Redis (expire 8 hours)
    await redis.set(
      `session:${gameSession.GameSessionId}`,
      JSON.stringify({ campaignId, gmId: req.user.id, status: 'ACTIVE' }),
      'EX', 28800
    );

    // Create GM's player session
    const playerSessionCmd = new CreatePlayerSessionCommand({
      GameSessionId: gameSession.GameSessionId,
      PlayerId:      req.user.id,
      PlayerData:    JSON.stringify({ role: 'gm', username: req.user.username }),
    });

    const psResponse = await gamelift.send(playerSessionCmd);

    res.status(201).json({
      gameSessionId:    gameSession.GameSessionId,
      playerSessionId:  psResponse.PlayerSession.PlayerSessionId,
      serverEndpoint:   `${psResponse.PlayerSession.IpAddress}:${psResponse.PlayerSession.Port}`,
      status:           gameSession.Status,
    });

  } catch (err) {
    console.error('[GameLift] CreateSession error:', err);
    res.status(500).json({ error: 'Failed to create game session', detail: err.message });
  }
});

app.post('/api/sessions/:sessionId/join', authMiddleware, async (req, res) => {
  const { sessionId } = req.params;

  try {
    // Check session exists and has capacity
    const cached = await redis.get(`session:${sessionId}`);
    if (!cached) return res.status(404).json({ error: 'Session not found or expired' });

    const command = new CreatePlayerSessionCommand({
      GameSessionId: sessionId,
      PlayerId:      req.user.id,
      PlayerData:    JSON.stringify({ role: 'player', username: req.user.username }),
    });

    const response = await gamelift.send(command);
    const ps = response.PlayerSession;

    res.json({
      playerSessionId:  ps.PlayerSessionId,
      serverEndpoint:   `${ps.IpAddress}:${ps.Port}`,
      status:           ps.Status,
    });

  } catch (err) {
    if (err.name === 'GameSessionFullException') {
      return res.status(409).json({ error: 'Session is full' });
    }
    res.status(500).json({ error: 'Failed to join session', detail: err.message });
  }
});

app.get('/api/sessions/active', authMiddleware, async (req, res) => {
  try {
    const command = new SearchGameSessionsCommand({
      FleetId:       GAMELIFT_FLEET,
      FilterExpression: 'hasAvailablePlayerSessions=true',
      SortExpression:   'creationTime DESC',
      Limit: 20,
    });

    const response = await gamelift.send(command);
    const sessions = (response.GameSessions || []).map(s => ({
      id:          s.GameSessionId,
      name:        s.Name,
      players:     s.CurrentPlayerSessionCount,
      maxPlayers:  s.MaximumPlayerSessionCount,
      status:      s.Status,
      campaignId:  s.GameProperties?.find(p => p.Key === 'CampaignId')?.Value,
      gmName:      s.GameProperties?.find(p => p.Key === 'GMName')?.Value,
      createdAt:   s.CreationTime,
    }));

    res.json(sessions);
  } catch (err) {
    res.status(500).json({ error: 'Failed to list sessions' });
  }
});

// ═══════════════════════════════════════════════════════════════
// ASSET UPLOAD ROUTES (S3 presigned URLs)
// ═══════════════════════════════════════════════════════════════

app.post('/api/assets/upload-url', authMiddleware, async (req, res) => {
  const { filename, contentType, folder = 'misc' } = req.body;

  const allowedFolders = ['maps', 'miniatures', 'audio', 'mods', 'portraits', 'misc'];
  if (!allowedFolders.includes(folder)) {
    return res.status(400).json({ error: 'Invalid folder' });
  }

  const allowedTypes = ['image/png', 'image/jpeg', 'image/webp', 'audio/ogg', 'audio/wav',
                        'application/octet-stream', 'model/gltf-binary', 'application/zip'];
  if (!allowedTypes.includes(contentType)) {
    return res.status(400).json({ error: 'File type not allowed' });
  }

  const key = `${folder}/${req.user.id}/${uuidv4()}-${filename}`;

  const command = new PutObjectCommand({
    Bucket:       ASSETS_BUCKET,
    Key:          key,
    ContentType:  contentType,
    Metadata:     { uploadedBy: req.user.id, originalName: filename },
  });

  const uploadUrl = await getSignedUrl(s3, command, { expiresIn: 300 });

  // Record in DB
  await db.query(
    `INSERT INTO assets (id, key, filename, content_type, folder, owner_id, created_at)
     VALUES ($1, $2, $3, $4, $5, $6, NOW())`,
    [uuidv4(), key, filename, contentType, folder, req.user.id]
  );

  res.json({ uploadUrl, key, cdnUrl: `https://${process.env.DOMAIN}/assets/${key}` });
});

app.get('/api/assets', authMiddleware, async (req, res) => {
  const { folder } = req.query;
  const { rows } = await db.query(
    `SELECT id, key, filename, content_type, folder, created_at,
            concat('https://${process.env.DOMAIN}/assets/', key) as url
     FROM assets
     WHERE owner_id = $1 ${folder ? 'AND folder = $2' : ''}
     ORDER BY created_at DESC`,
    folder ? [req.user.id, folder] : [req.user.id]
  );
  res.json(rows);
});

app.delete('/api/assets/:id', authMiddleware, async (req, res) => {
  const { rows } = await db.query(
    'SELECT key FROM assets WHERE id = $1 AND owner_id = $2',
    [req.params.id, req.user.id]
  );
  if (!rows[0]) return res.status(404).json({ error: 'Asset not found' });

  await s3.send(new DeleteObjectCommand({ Bucket: ASSETS_BUCKET, Key: rows[0].key }));
  await db.query('DELETE FROM assets WHERE id = $1', [req.params.id]);
  res.json({ success: true });
});

// ═══════════════════════════════════════════════════════════════
// WEBSOCKET — Real-time session coordination
# (dice roll notifications, fog updates, chat, initiative sync)
// ═══════════════════════════════════════════════════════════════

const wss = new WebSocketServer({ server, path: '/ws' });

// Track connections by userId and sessionId
const connections = new Map(); // sessionId -> Set<ws>

function broadcast(sessionId, message, excludeWs = null) {
  const sessionConns = connections.get(sessionId);
  if (!sessionConns) return;
  const data = JSON.stringify(message);
  sessionConns.forEach(ws => {
    if (ws !== excludeWs && ws.readyState === 1) {
      ws.send(data);
    }
  });
}

wss.on('connection', (ws, req) => {
  let userId = null;
  let sessionId = null;

  ws.on('message', async (raw) => {
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }

    switch (msg.type) {

      case 'auth': {
        try {
          const decoded = jwt.verify(msg.token, JWT_SECRET);
          userId = decoded.id;
          ws.username = decoded.username;
          ws.send(JSON.stringify({ type: 'auth_ok', userId }));
        } catch {
          ws.send(JSON.stringify({ type: 'auth_error' }));
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

        // Persist presence
        await redis.sadd(`session_users:${sessionId}`, userId);
        await redis.expire(`session_users:${sessionId}`, 28800);

        broadcast(sessionId, { type: 'player_joined', username: ws.username, userId }, ws);
        console.log(`[WS] ${ws.username} joined session ${sessionId}`);
        break;
      }

      case 'dice_roll': {
        // Relay dice roll to all session participants
        if (!sessionId) return;
        const rollEvent = { type: 'dice_roll', ...msg, username: ws.username, timestamp: Date.now() };

        if (msg.visibility === 'public') {
          broadcast(sessionId, rollEvent);
        } else if (msg.visibility === 'gm_only') {
          // Only send to GM — look up GM's ws
          connections.get(sessionId)?.forEach(c => {
            if (c.role === 'gm') c.send(JSON.stringify(rollEvent));
          });
        }
        // Log to DB
        await db.query(
          `INSERT INTO roll_log (id, session_id, user_id, formula, result, visibility, rolled_at)
           VALUES ($1, $2, $3, $4, $5, $6, NOW())`,
          [uuidv4(), sessionId, userId, msg.formula, JSON.stringify(msg.result), msg.visibility || 'public']
        );
        break;
      }

      case 'fog_update': {
        if (!sessionId) return;
        // Cache fog state in Redis
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
        const chatMsg = { type: 'chat_message', text: msg.text, username: ws.username,
                          style: msg.style || 'normal', timestamp: Date.now() };
        broadcast(sessionId, chatMsg);
        break;
      }

      case 'miniature_move': {
        if (!sessionId) return;
        broadcast(sessionId, { type: 'miniature_move', ...msg, username: ws.username }, ws);
        break;
      }

      case 'ping': {
        ws.send(JSON.stringify({ type: 'pong' }));
        break;
      }
    }
  });

  ws.on('close', async () => {
    if (sessionId && userId) {
      connections.get(sessionId)?.delete(ws);
      if (connections.get(sessionId)?.size === 0) connections.delete(sessionId);
      await redis.srem(`session_users:${sessionId}`, userId);
      broadcast(sessionId, { type: 'player_left', username: ws.username, userId });
    }
  });

  ws.on('error', (err) => console.error('[WS] Error:', err));
});

// ─── Error Handler ─────────────────────────────────────────────
app.use((err, req, res, next) => {
  console.error('[API] Unhandled error:', err);
  res.status(500).json({ error: 'Internal server error' });
});

// ─── DB Schema Init ────────────────────────────────────────────
async function initDb() {
  await db.query(`
    CREATE TABLE IF NOT EXISTS users (
      id UUID PRIMARY KEY,
      username TEXT UNIQUE NOT NULL,
      email TEXT UNIQUE NOT NULL,
      password_hash TEXT NOT NULL,
      role TEXT DEFAULT 'player',
      created_at TIMESTAMPTZ,
      last_login TIMESTAMPTZ
    );

    CREATE TABLE IF NOT EXISTS campaigns (
      id UUID PRIMARY KEY,
      title TEXT NOT NULL,
      description TEXT,
      setting TEXT DEFAULT 'Fantasy',
      system TEXT DEFAULT 'D&D 5e',
      gm_id UUID REFERENCES users(id),
      active_map TEXT,
      world_state JSONB DEFAULT '{}',
      created_at TIMESTAMPTZ,
      updated_at TIMESTAMPTZ
    );

    CREATE TABLE IF NOT EXISTS journal_entries (
      id UUID PRIMARY KEY,
      campaign_id UUID REFERENCES campaigns(id),
      title TEXT NOT NULL,
      body TEXT,
      author_id UUID REFERENCES users(id),
      is_gm_only BOOLEAN DEFAULT false,
      created_at TIMESTAMPTZ
    );

    CREATE TABLE IF NOT EXISTS game_sessions (
      id UUID PRIMARY KEY,
      campaign_id UUID REFERENCES campaigns(id),
      gamelift_session_id TEXT,
      status TEXT,
      gm_id UUID REFERENCES users(id),
      created_at TIMESTAMPTZ,
      ended_at TIMESTAMPTZ
    );

    CREATE TABLE IF NOT EXISTS roll_log (
      id UUID PRIMARY KEY,
      session_id TEXT,
      user_id UUID REFERENCES users(id),
      formula TEXT,
      result JSONB,
      visibility TEXT,
      rolled_at TIMESTAMPTZ
    );

    CREATE TABLE IF NOT EXISTS assets (
      id UUID PRIMARY KEY,
      key TEXT NOT NULL,
      filename TEXT,
      content_type TEXT,
      folder TEXT,
      owner_id UUID REFERENCES users(id),
      created_at TIMESTAMPTZ
    );

    CREATE INDEX IF NOT EXISTS idx_campaigns_gm ON campaigns(gm_id);
    CREATE INDEX IF NOT EXISTS idx_sessions_campaign ON game_sessions(campaign_id);
    CREATE INDEX IF NOT EXISTS idx_assets_owner ON assets(owner_id);
  `);
  console.log('[DB] Schema initialized');
}

// ─── Start ─────────────────────────────────────────────────────
initDb().then(() => {
  server.listen(PORT, () => {
    console.log(`[RealmForge] Dashboard API + WS running on :${PORT}`);
    console.log(`[RealmForge] Environment: ${process.env.NODE_ENV}`);
    console.log(`[RealmForge] GameLift Fleet: ${GAMELIFT_FLEET}`);
  });
}).catch(err => {
  console.error('[Startup] Fatal:', err);
  process.exit(1);
});

export default app;
