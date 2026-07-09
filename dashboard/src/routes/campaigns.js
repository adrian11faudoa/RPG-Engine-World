/**
 * dashboard/src/routes/campaigns.js
 */

import { Router } from 'express';
import { v4 as uuidv4 } from 'uuid';
import { db }    from '../db.js';
import { authMiddleware, gmMiddleware } from '../middleware/auth.js';

const router = Router();
router.use(authMiddleware);

// ─── Campaigns CRUD ───────────────────────────────────────────

// GET /api/campaigns
router.get('/', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      `SELECT c.*, u.username AS gm_name,
              COUNT(DISTINCT cp.user_id)::int AS player_count
       FROM campaigns c
       JOIN users u ON c.gm_id = u.id
       LEFT JOIN campaign_players cp ON cp.campaign_id = c.id
       WHERE c.gm_id = $1
          OR c.id IN (SELECT campaign_id FROM campaign_players WHERE user_id = $1)
       GROUP BY c.id, u.username
       ORDER BY c.updated_at DESC`,
      [req.user.id]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

// POST /api/campaigns
router.post('/', async (req, res, next) => {
  try {
    const { title, description, setting = 'Fantasy', system = 'D&D 5e' } = req.body;
    if (!title) return res.status(400).json({ error: 'title is required' });

    const { rows } = await db.query(
      `INSERT INTO campaigns (id, title, description, setting, system, gm_id, created_at, updated_at)
       VALUES ($1, $2, $3, $4, $5, $6, NOW(), NOW()) RETURNING *`,
      [uuidv4(), title, description, setting, system, req.user.id]
    );
    res.status(201).json(rows[0]);
  } catch (err) { next(err); }
});

// GET /api/campaigns/:id
router.get('/:id', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      `SELECT c.*, u.username AS gm_name FROM campaigns c
       JOIN users u ON c.gm_id = u.id WHERE c.id = $1`,
      [req.params.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'Campaign not found' });
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// PATCH /api/campaigns/:id
router.patch('/:id', gmMiddleware, async (req, res, next) => {
  try {
    const { title, description, world_state, active_map, setting, system } = req.body;
    const { rows } = await db.query(
      `UPDATE campaigns SET
         title       = COALESCE($1, title),
         description = COALESCE($2, description),
         world_state = COALESCE($3, world_state),
         active_map  = COALESCE($4, active_map),
         setting     = COALESCE($5, setting),
         system      = COALESCE($6, system),
         updated_at  = NOW()
       WHERE id = $7 AND gm_id = $8 RETURNING *`,
      [title, description, world_state, active_map, setting, system, req.params.id, req.user.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'Campaign not found or not your campaign' });
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// DELETE /api/campaigns/:id
router.delete('/:id', gmMiddleware, async (req, res, next) => {
  try {
    const { rowCount } = await db.query(
      'DELETE FROM campaigns WHERE id = $1 AND gm_id = $2',
      [req.params.id, req.user.id]
    );
    if (!rowCount) return res.status(404).json({ error: 'Campaign not found' });
    res.json({ success: true });
  } catch (err) { next(err); }
});

// ─── World State (key-value flags) ────────────────────────────

// GET /api/campaigns/:id/state/:key
router.get('/:id/state/:key', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      `SELECT world_state->$1 AS value FROM campaigns WHERE id = $2`,
      [req.params.key, req.params.id]
    );
    res.json({ key: req.params.key, value: rows[0]?.value ?? null });
  } catch (err) { next(err); }
});

// PUT /api/campaigns/:id/state/:key
router.put('/:id/state/:key', gmMiddleware, async (req, res, next) => {
  try {
    const { value } = req.body;
    await db.query(
      `UPDATE campaigns SET world_state = jsonb_set(world_state, $1, $2::jsonb), updated_at = NOW()
       WHERE id = $3 AND gm_id = $4`,
      [`{${req.params.key}}`, JSON.stringify(value), req.params.id, req.user.id]
    );
    res.json({ key: req.params.key, value });
  } catch (err) { next(err); }
});

// ─── Journal ──────────────────────────────────────────────────

// GET /api/campaigns/:id/journal
router.get('/:id/journal', async (req, res, next) => {
  try {
    const isGM = req.user.role === 'gm' || req.user.role === 'admin';
    const { rows } = await db.query(
      `SELECT j.*, u.username AS author_name FROM journal_entries j
       LEFT JOIN users u ON j.author_id = u.id
       WHERE j.campaign_id = $1 ${!isGM ? 'AND j.is_gm_only = false' : ''}
       ORDER BY j.created_at DESC`,
      [req.params.id]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

// POST /api/campaigns/:id/journal
router.post('/:id/journal', async (req, res, next) => {
  try {
    const { title, body, is_gm_only = false } = req.body;
    if (!title) return res.status(400).json({ error: 'title is required' });
    const { rows } = await db.query(
      `INSERT INTO journal_entries (id, campaign_id, title, body, author_id, is_gm_only, created_at, updated_at)
       VALUES ($1, $2, $3, $4, $5, $6, NOW(), NOW()) RETURNING *`,
      [uuidv4(), req.params.id, title, body, req.user.id, is_gm_only]
    );
    res.status(201).json(rows[0]);
  } catch (err) { next(err); }
});

// PATCH /api/campaigns/:id/journal/:entryId
router.patch('/:id/journal/:entryId', async (req, res, next) => {
  try {
    const { title, body } = req.body;
    const { rows } = await db.query(
      `UPDATE journal_entries SET
         title = COALESCE($1, title),
         body  = COALESCE($2, body),
         updated_at = NOW()
       WHERE id = $3 AND campaign_id = $4 AND author_id = $5 RETURNING *`,
      [title, body, req.params.entryId, req.params.id, req.user.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'Entry not found' });
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// ─── Quests ───────────────────────────────────────────────────

// GET /api/campaigns/:id/quests
router.get('/:id/quests', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      'SELECT * FROM quests WHERE campaign_id = $1 ORDER BY created_at ASC',
      [req.params.id]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

// POST /api/campaigns/:id/quests
router.post('/:id/quests', gmMiddleware, async (req, res, next) => {
  try {
    const { title, description, objectives = [] } = req.body;
    if (!title) return res.status(400).json({ error: 'title is required' });
    const { rows } = await db.query(
      `INSERT INTO quests (id, campaign_id, title, description, objectives, created_at)
       VALUES ($1, $2, $3, $4, $5, NOW()) RETURNING *`,
      [uuidv4(), req.params.id, title, description, JSON.stringify(objectives)]
    );
    res.status(201).json(rows[0]);
  } catch (err) { next(err); }
});

// PATCH /api/campaigns/:id/quests/:questId
router.patch('/:id/quests/:questId', gmMiddleware, async (req, res, next) => {
  try {
    const { is_complete, objectives } = req.body;
    const { rows } = await db.query(
      `UPDATE quests SET
         is_complete = COALESCE($1, is_complete),
         objectives  = COALESCE($2, objectives)
       WHERE id = $3 AND campaign_id = $4 RETURNING *`,
      [is_complete, objectives ? JSON.stringify(objectives) : null,
       req.params.questId, req.params.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'Quest not found' });
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// ─── NPCs ─────────────────────────────────────────────────────

// GET /api/campaigns/:id/npcs
router.get('/:id/npcs', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      'SELECT * FROM npcs WHERE campaign_id = $1 ORDER BY name ASC',
      [req.params.id]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

// POST /api/campaigns/:id/npcs
router.post('/:id/npcs', gmMiddleware, async (req, res, next) => {
  try {
    const { name, description, location, attitude = 'neutral' } = req.body;
    if (!name) return res.status(400).json({ error: 'name is required' });
    const { rows } = await db.query(
      `INSERT INTO npcs (id, campaign_id, name, description, location, attitude, created_at)
       VALUES ($1, $2, $3, $4, $5, $6, NOW()) RETURNING *`,
      [uuidv4(), req.params.id, name, description, location, attitude]
    );
    res.status(201).json(rows[0]);
  } catch (err) { next(err); }
});

// PATCH /api/campaigns/:id/npcs/:npcId
router.patch('/:id/npcs/:npcId', gmMiddleware, async (req, res, next) => {
  try {
    const { description, location, attitude, is_alive, notes } = req.body;
    const { rows } = await db.query(
      `UPDATE npcs SET
         description = COALESCE($1, description),
         location    = COALESCE($2, location),
         attitude    = COALESCE($3, attitude),
         is_alive    = COALESCE($4, is_alive),
         notes       = CASE WHEN $5::jsonb IS NOT NULL THEN notes || $5::jsonb ELSE notes END
       WHERE id = $6 AND campaign_id = $7 RETURNING *`,
      [description, location, attitude, is_alive,
       notes ? JSON.stringify(notes) : null,
       req.params.npcId, req.params.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'NPC not found' });
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// ─── Roll Log ─────────────────────────────────────────────────

// GET /api/campaigns/:id/rolls
router.get('/:id/rolls', async (req, res, next) => {
  try {
    const limit  = Math.min(parseInt(req.query.limit  || '50'),  200);
    const offset = parseInt(req.query.offset || '0');
    const { rows } = await db.query(
      `SELECT r.*, u.username FROM roll_log r
       LEFT JOIN users u ON r.user_id = u.id
       WHERE r.campaign_id = $1
       ORDER BY r.rolled_at DESC LIMIT $2 OFFSET $3`,
      [req.params.id, limit, offset]
    );
    res.json(rows);
  } catch (err) { next(err); }
});

export default router;
