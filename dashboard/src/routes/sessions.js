/**
 * dashboard/src/routes/sessions.js
 * GameLift game session management
 */

import { Router } from 'express';
import { v4 as uuidv4 } from 'uuid';
import {
  GameLiftClient,
  CreateGameSessionCommand,
  CreatePlayerSessionCommand,
  SearchGameSessionsCommand,
  DescribeGameSessionsCommand,
  TerminateGameSessionCommand,
} from '@aws-sdk/client-gamelift';
import { db, redis } from '../db.js';
import { authMiddleware, gmMiddleware } from '../middleware/auth.js';
import { perUserStrictLimiter, perUserLimiter } from '../middleware/rateLimiter.js';

const router  = Router();
const gamelift = new GameLiftClient({ region: process.env.AWS_REGION || 'us-east-1' });

const FLEET_ID  = process.env.GAMELIFT_FLEET_ID;
const QUEUE     = process.env.GAMELIFT_QUEUE;
const SESSION_TTL = 60 * 60 * 8; // 8 hours

router.use(authMiddleware);

// ─── Create session (GM only) ─────────────────────────────────

// POST /api/sessions/create
router.post('/create', gmMiddleware, perUserStrictLimiter, async (req, res, next) => {
  try {
    const { campaignId, maxPlayers = 8, sessionName } = req.body;
    if (!campaignId) return res.status(400).json({ error: 'campaignId is required' });

    // Confirm requester is GM of this campaign
    const { rows: cam } = await db.query(
      'SELECT id, title FROM campaigns WHERE id = $1 AND gm_id = $2',
      [campaignId, req.user.id]
    );
    if (!cam[0]) return res.status(403).json({ error: 'You must be the GM of this campaign' });

    const name = sessionName || `${cam[0].title} — ${req.user.username}`;

    const createCmd = new CreateGameSessionCommand({
      FleetId:                    FLEET_ID,
      MaximumPlayerSessionCount:  Math.min(Math.max(maxPlayers, 1), 8),
      Name:                       name,
      GameProperties: [
        { Key: 'CampaignId', Value: campaignId },
        { Key: 'GMId',       Value: req.user.id },
        { Key: 'GMName',     Value: req.user.username },
      ],
    });

    const { GameSession: gs } = await gamelift.send(createCmd);

    // Persist to DB
    const sessionId = uuidv4();
    await db.query(
      `INSERT INTO game_sessions (id, campaign_id, gamelift_session_id, status, gm_id, created_at)
       VALUES ($1, $2, $3, 'ACTIVE', $4, NOW())`,
      [sessionId, campaignId, gs.GameSessionId, req.user.id]
    );

    // Cache in Redis
    await redis.set(
      `session:${gs.GameSessionId}`,
      JSON.stringify({ campaignId, gmId: req.user.id, gmName: req.user.username }),
      'EX', SESSION_TTL
    );

    // Create GM's player session immediately
    const playerCmd = new CreatePlayerSessionCommand({
      GameSessionId: gs.GameSessionId,
      PlayerId:      req.user.id,
      PlayerData:    JSON.stringify({ role: 'gm', username: req.user.username }),
    });
    const { PlayerSession: ps } = await gamelift.send(playerCmd);

    res.status(201).json({
      gameSessionId:   gs.GameSessionId,
      playerSessionId: ps.PlayerSessionId,
      serverEndpoint:  `${ps.IpAddress}:${ps.Port}`,
      status:          gs.Status,
      name,
    });

  } catch (err) {
    if (err.name === 'FleetCapacityExceededException') {
      return res.status(503).json({ error: 'No server capacity available right now. Try again in a moment.' });
    }
    next(err);
  }
});

// ─── Join session (any authenticated user) ────────────────────

// POST /api/sessions/:sessionId/join
router.post('/:sessionId/join', authMiddleware, perUserLimiter, async (req, res, next) => {
  try {
    const { sessionId } = req.params;

    // Check session is known and active
    const cached = await redis.get(`session:${sessionId}`);
    if (!cached) {
      // Fall back to DB
      const { rows } = await db.query(
        "SELECT gamelift_session_id FROM game_sessions WHERE gamelift_session_id = $1 AND status = 'ACTIVE'",
        [sessionId]
      );
      if (!rows[0]) return res.status(404).json({ error: 'Session not found or has ended' });
    }

    const cmd = new CreatePlayerSessionCommand({
      GameSessionId: sessionId,
      PlayerId:      req.user.id,
      PlayerData:    JSON.stringify({ role: 'player', username: req.user.username }),
    });

    const { PlayerSession: ps } = await gamelift.send(cmd);

    // Track player in Redis presence set
    await redis.sadd(`session_users:${sessionId}`, req.user.id);
    await redis.expire(`session_users:${sessionId}`, SESSION_TTL);

    res.json({
      playerSessionId: ps.PlayerSessionId,
      serverEndpoint:  `${ps.IpAddress}:${ps.Port}`,
      status:          ps.Status,
    });

  } catch (err) {
    if (err.name === 'GameSessionFullException') {
      return res.status(409).json({ error: 'Session is full' });
    }
    if (err.name === 'InvalidGameSessionStatusException') {
      return res.status(409).json({ error: 'Session is no longer accepting players' });
    }
    next(err);
  }
});

// ─── List active sessions ─────────────────────────────────────

// GET /api/sessions/active
router.get('/active', async (req, res, next) => {
  try {
    const cmd = new SearchGameSessionsCommand({
      FleetId:          FLEET_ID,
      FilterExpression: 'hasAvailablePlayerSessions=true',
      SortExpression:   'creationTime DESC',
      Limit:            20,
    });

    const { GameSessions: sessions = [] } = await gamelift.send(cmd);

    const result = sessions.map(s => ({
      id:         s.GameSessionId,
      name:       s.Name,
      players:    s.CurrentPlayerSessionCount,
      maxPlayers: s.MaximumPlayerSessionCount,
      status:     s.Status,
      campaignId: s.GameProperties?.find(p => p.Key === 'CampaignId')?.Value,
      gmName:     s.GameProperties?.find(p => p.Key === 'GMName')?.Value,
      createdAt:  s.CreationTime,
    }));

    res.json(result);
  } catch (err) { next(err); }
});

// ─── Get session details ──────────────────────────────────────

// GET /api/sessions/:sessionId
router.get('/:sessionId', async (req, res, next) => {
  try {
    const cmd = new DescribeGameSessionsCommand({
      GameSessionId: req.params.sessionId,
    });
    const { GameSessions: [gs] = [] } = await gamelift.send(cmd);
    if (!gs) return res.status(404).json({ error: 'Session not found' });

    // Enrich with presence data from Redis
    const userIds = await redis.smembers(`session_users:${req.params.sessionId}`);

    res.json({
      id:          gs.GameSessionId,
      name:        gs.Name,
      players:     gs.CurrentPlayerSessionCount,
      maxPlayers:  gs.MaximumPlayerSessionCount,
      status:      gs.Status,
      campaignId:  gs.GameProperties?.find(p => p.Key === 'CampaignId')?.Value,
      gmId:        gs.GameProperties?.find(p => p.Key === 'GMId')?.Value,
      gmName:      gs.GameProperties?.find(p => p.Key === 'GMName')?.Value,
      connectedUserIds: userIds,
    });
  } catch (err) { next(err); }
});

// ─── End session (GM only) ────────────────────────────────────

// POST /api/sessions/:sessionId/end
router.post('/:sessionId/end', gmMiddleware, async (req, res, next) => {
  try {
    // Mark as ended in DB
    await db.query(
      "UPDATE game_sessions SET status = 'ENDED', ended_at = NOW() WHERE gamelift_session_id = $1 AND gm_id = $2",
      [req.params.sessionId, req.user.id]
    );

    // Clean up Redis
    await redis.del(`session:${req.params.sessionId}`);
    await redis.del(`session_users:${req.params.sessionId}`);
    await redis.del(`fog:${req.params.sessionId}`);
    await redis.del(`initiative:${req.params.sessionId}`);

    res.json({ success: true });
  } catch (err) { next(err); }
});

// ─── Get session roll history ─────────────────────────────────

// GET /api/sessions/:sessionId/rolls
router.get('/:sessionId/rolls', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      `SELECT r.*, u.username FROM roll_log r
       LEFT JOIN users u ON r.user_id = u.id
       WHERE r.session_id = $1
       ORDER BY r.rolled_at DESC LIMIT 100`,
      [req.params.sessionId]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

export default router;
