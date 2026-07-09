/**
 * dashboard/src/routes/admin.js
 * Admin-only endpoints for user management, campaign oversight,
 * system health stats, and moderation tools.
 * All routes require role = 'admin'.
 */

import { Router }        from 'express';
import { db, redis }     from '../db.js';
import { authMiddleware, adminMiddleware } from '../middleware/auth.js';
import {
  GameLiftClient,
  DescribeFleetCapacityCommand,
  DescribeFleetAttributesCommand,
  UpdateFleetCapacityCommand,
} from '@aws-sdk/client-gamelift';
import { v4 as uuidv4 }  from 'uuid';

const router    = Router();
const gamelift  = new GameLiftClient({ region: process.env.AWS_REGION || 'us-east-1' });
const FLEET_ID  = process.env.GAMELIFT_FLEET;

router.use(authMiddleware, adminMiddleware);

// ═══════════════════════════════════════════════════════════════
// DASHBOARD STATS
// ═══════════════════════════════════════════════════════════════

/**
 * GET /api/admin/stats
 * Aggregate system stats — shown on the admin dashboard.
 */
router.get('/stats', async (req, res, next) => {
  try {
    const [users, campaigns, sessions, rolls, assets, redisInfo] = await Promise.all([
      db.query('SELECT COUNT(*) AS total, COUNT(*) FILTER (WHERE last_login > NOW() - INTERVAL \'7 days\') AS active_7d FROM users'),
      db.query('SELECT COUNT(*) AS total FROM campaigns'),
      db.query(`SELECT COUNT(*) AS total,
                       COUNT(*) FILTER (WHERE status = 'ACTIVE') AS active,
                       COUNT(*) FILTER (WHERE created_at > NOW() - INTERVAL '24 hours') AS today
                FROM game_sessions`),
      db.query('SELECT COUNT(*) AS total FROM roll_log WHERE rolled_at > NOW() - INTERVAL \'24 hours\''),
      db.query('SELECT COUNT(*) AS total, COALESCE(SUM(size_bytes), 0) AS total_bytes FROM assets'),
      redis.info('memory'),
    ]);

    // GameLift fleet capacity
    let fleetCapacity = null;
    if (FLEET_ID) {
      try {
        const resp = await gamelift.send(
          new DescribeFleetCapacityCommand({ FleetIds: [FLEET_ID] })
        );
        const cap = resp.FleetCapacity?.[0];
        fleetCapacity = {
          desired:   cap?.InstanceCounts?.DESIRED   || 0,
          active:    cap?.InstanceCounts?.ACTIVE     || 0,
          idle:      cap?.InstanceCounts?.IDLE       || 0,
          pending:   cap?.InstanceCounts?.PENDING    || 0,
          min:       cap?.InstanceCounts?.MINIMUM    || 0,
          max:       cap?.InstanceCounts?.MAXIMUM    || 0,
        };
      } catch (err) {
        fleetCapacity = { error: err.message };
      }
    }

    // Parse Redis memory usage
    const redisMemMatch = redisInfo.match(/used_memory_human:(\S+)/);

    res.json({
      users: {
        total:    parseInt(users.rows[0].total),
        active7d: parseInt(users.rows[0].active_7d),
      },
      campaigns: {
        total: parseInt(campaigns.rows[0].total),
      },
      sessions: {
        total:  parseInt(sessions.rows[0].total),
        active: parseInt(sessions.rows[0].active),
        today:  parseInt(sessions.rows[0].today),
      },
      rolls: {
        last24h: parseInt(rolls.rows[0].total),
      },
      assets: {
        total:      parseInt(assets.rows[0].total),
        totalBytes: parseInt(assets.rows[0].total_bytes),
      },
      redis: {
        memoryUsed: redisMemMatch?.[1] || 'unknown',
      },
      gamelift: fleetCapacity,
      generatedAt: new Date().toISOString(),
    });
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// USER MANAGEMENT
// ═══════════════════════════════════════════════════════════════

/** GET /api/admin/users — paginated user list */
router.get('/users', async (req, res, next) => {
  try {
    const page     = Math.max(1, parseInt(req.query.page  || '1'));
    const limit    = Math.min(100, parseInt(req.query.limit || '50'));
    const offset   = (page - 1) * limit;
    const search   = req.query.search || '';
    const role     = req.query.role   || '';

    const conditions = [];
    const params     = [];
    let   paramIdx   = 1;

    if (search) {
      conditions.push(`(username ILIKE $${paramIdx} OR email ILIKE $${paramIdx})`);
      params.push(`%${search}%`);
      paramIdx++;
    }
    if (role) {
      conditions.push(`role = $${paramIdx}`);
      params.push(role);
      paramIdx++;
    }

    const where = conditions.length ? `WHERE ${conditions.join(' AND ')}` : '';

    const [rows, count] = await Promise.all([
      db.query(
        `SELECT id, username, email, role, created_at, last_login
         FROM users ${where}
         ORDER BY created_at DESC
         LIMIT $${paramIdx} OFFSET $${paramIdx + 1}`,
        [...params, limit, offset]
      ),
      db.query(`SELECT COUNT(*) FROM users ${where}`, params),
    ]);

    res.json({
      users:   rows.rows,
      total:   parseInt(count.rows[0].count),
      page,
      limit,
      pages:   Math.ceil(count.rows[0].count / limit),
    });
  } catch (err) {
    next(err);
  }
});

/** PATCH /api/admin/users/:id — update role or status */
router.patch('/users/:id', async (req, res, next) => {
  try {
    const { role } = req.body;
    const validRoles = ['player', 'gm', 'admin'];

    if (role && !validRoles.includes(role)) {
      return res.status(400).json({ error: `role must be one of: ${validRoles.join(', ')}` });
    }

    // Prevent demoting the only admin
    if (role && role !== 'admin') {
      const { rows } = await db.query(
        "SELECT COUNT(*) FROM users WHERE role = 'admin' AND id != $1",
        [req.params.id]
      );
      if (parseInt(rows[0].count) === 0) {
        return res.status(409).json({ error: 'Cannot demote the only admin account' });
      }
    }

    const { rows } = await db.query(
      `UPDATE users SET role = COALESCE($1, role)
       WHERE id = $2
       RETURNING id, username, email, role`,
      [role, req.params.id]
    );

    if (!rows[0]) return res.status(404).json({ error: 'User not found' });
    res.json(rows[0]);
  } catch (err) {
    next(err);
  }
});

/** DELETE /api/admin/users/:id — permanently delete account */
router.delete('/users/:id', async (req, res, next) => {
  try {
    if (req.params.id === req.user.id) {
      return res.status(409).json({ error: 'Cannot delete your own account' });
    }
    const { rowCount } = await db.query('DELETE FROM users WHERE id = $1', [req.params.id]);
    if (rowCount === 0) return res.status(404).json({ error: 'User not found' });
    res.json({ success: true });
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// CAMPAIGN OVERSIGHT
// ═══════════════════════════════════════════════════════════════

/** GET /api/admin/campaigns — all campaigns with metadata */
router.get('/campaigns', async (req, res, next) => {
  try {
    const page   = Math.max(1, parseInt(req.query.page  || '1'));
    const limit  = Math.min(100, parseInt(req.query.limit || '50'));
    const offset = (page - 1) * limit;

    const { rows } = await db.query(
      `SELECT c.id, c.title, c.created_at, c.updated_at,
              u.username AS gm_name, u.email AS gm_email,
              COUNT(DISTINCT cp.user_id) AS player_count,
              COUNT(DISTINCT gs.id)      AS session_count
       FROM campaigns c
       JOIN users u ON c.gm_id = u.id
       LEFT JOIN campaign_players cp ON cp.campaign_id = c.id
       LEFT JOIN game_sessions gs ON gs.campaign_id = c.id
       GROUP BY c.id, u.username, u.email
       ORDER BY c.updated_at DESC
       LIMIT $1 OFFSET $2`,
      [limit, offset]
    );
    res.json(rows);
  } catch (err) {
    next(err);
  }
});

/** DELETE /api/admin/campaigns/:id — force-delete any campaign */
router.delete('/campaigns/:id', async (req, res, next) => {
  try {
    const { rowCount } = await db.query(
      'DELETE FROM campaigns WHERE id = $1', [req.params.id]
    );
    if (rowCount === 0) return res.status(404).json({ error: 'Campaign not found' });
    res.json({ success: true });
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// GAMELIFT FLEET MANAGEMENT
// ═══════════════════════════════════════════════════════════════

/** GET /api/admin/fleet — fleet capacity and status */
router.get('/fleet', async (req, res, next) => {
  try {
    if (!FLEET_ID) return res.status(503).json({ error: 'GAMELIFT_FLEET not configured' });

    const [capacity, attributes] = await Promise.all([
      gamelift.send(new DescribeFleetCapacityCommand({ FleetIds: [FLEET_ID] })),
      gamelift.send(new DescribeFleetAttributesCommand({ FleetIds: [FLEET_ID] })),
    ]);

    res.json({
      capacity:   capacity.FleetCapacity?.[0],
      attributes: attributes.FleetAttributes?.[0],
    });
  } catch (err) {
    next(err);
  }
});

/** PATCH /api/admin/fleet/capacity — scale fleet up or down */
router.patch('/fleet/capacity', async (req, res, next) => {
  try {
    if (!FLEET_ID) return res.status(503).json({ error: 'GAMELIFT_FLEET not configured' });

    const { desired, min, max } = req.body;
    if (desired === undefined && min === undefined && max === undefined) {
      return res.status(400).json({ error: 'Provide desired, min, or max' });
    }

    await gamelift.send(new UpdateFleetCapacityCommand({
      FleetId:          FLEET_ID,
      DesiredInstances: desired,
      MinSize:          min,
      MaxSize:          max,
    }));

    res.json({ success: true, fleetId: FLEET_ID, desired, min, max });
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// ROLL LOG AUDIT
// ═══════════════════════════════════════════════════════════════

/** GET /api/admin/rolls — recent roll log across all sessions */
router.get('/rolls', async (req, res, next) => {
  try {
    const limit = Math.min(500, parseInt(req.query.limit || '100'));
    const { rows } = await db.query(
      `SELECT rl.id, rl.formula, rl.result, rl.visibility, rl.rolled_at,
              u.username, rl.session_id, c.title AS campaign_title
       FROM roll_log rl
       LEFT JOIN users u ON rl.user_id = u.id
       LEFT JOIN campaigns c ON rl.campaign_id = c.id
       ORDER BY rl.rolled_at DESC
       LIMIT $1`,
      [limit]
    );
    res.json(rows);
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// SYSTEM CACHE MANAGEMENT
// ═══════════════════════════════════════════════════════════════

/** DELETE /api/admin/cache/sessions — flush all active session caches */
router.delete('/cache/sessions', async (req, res, next) => {
  try {
    const keys = await redis.keys('session:*');
    const userKeys = await redis.keys('session_users:*');
    const allKeys = [...keys, ...userKeys];

    if (allKeys.length > 0) await redis.del(...allKeys);

    res.json({ success: true, flushed: allKeys.length });
  } catch (err) {
    next(err);
  }
});

/** DELETE /api/admin/cache/fog — flush fog state for a session */
router.delete('/cache/fog/:sessionId', async (req, res, next) => {
  try {
    await redis.del(`fog:${req.params.sessionId}`);
    res.json({ success: true });
  } catch (err) {
    next(err);
  }
});

export default router;
