/**
 * dashboard/src/routes/campaigns.extra.js
 * Additional campaign endpoints:
 *   - Export campaign as JSON archive
 *   - Import campaign from JSON archive
 *   - Bulk asset upload (zip → extract → multi-file S3)
 *
 * These are registered in server.js under /api/campaigns
 */

import { Router }       from 'express';
import { db, redis }    from '../db.js';
import { authMiddleware, gmMiddleware } from '../middleware/auth.js';
import { perUserStrictLimiter }        from '../middleware/rateLimiter.js';
import {
  S3Client,
  PutObjectCommand,
  GetObjectCommand,
} from '@aws-sdk/client-s3';
import { getSignedUrl }  from '@aws-sdk/s3-request-presigner';
import { v4 as uuidv4 } from 'uuid';
import multer            from 'multer';
import AdmZip            from 'adm-zip';
import path              from 'path';

const router  = Router();
const s3      = new S3Client({ region: process.env.AWS_REGION || 'us-east-1' });
const BUCKET  = process.env.ASSETS_BUCKET;

// Multer for in-memory file handling (max 50MB zip)
const upload = multer({
  storage: multer.memoryStorage(),
  limits:  { fileSize: 50 * 1024 * 1024 },
  fileFilter: (req, file, cb) => {
    if (file.mimetype === 'application/zip' ||
        file.originalname.endsWith('.zip')) {
      cb(null, true);
    } else {
      cb(new Error('Only .zip files are accepted for bulk upload'));
    }
  },
});

// ═══════════════════════════════════════════════════════════════
// CAMPAIGN EXPORT
// ═══════════════════════════════════════════════════════════════

/**
 * GET /api/campaigns/:id/export
 * Exports a complete campaign as a JSON bundle including:
 * journal entries, quests, NPCs, roll logs, and world state.
 * Does NOT include binary assets (too large — these stay in S3).
 */
router.get('/:id/export', authMiddleware, async (req, res, next) => {
  try {
    const campaignId = req.params.id;

    // Verify requester owns this campaign or is a player in it
    const { rows: cam } = await db.query(
      `SELECT c.*, u.username AS gm_name
       FROM campaigns c
       JOIN users u ON c.gm_id = u.id
       WHERE c.id = $1 AND (c.gm_id = $2 OR c.id IN (
         SELECT campaign_id FROM campaign_players WHERE user_id = $2
       ))`,
      [campaignId, req.user.id]
    );

    if (!cam[0]) return res.status(404).json({ error: 'Campaign not found or access denied' });

    // Gather all campaign data in parallel
    const [journal, quests, npcs, rolls] = await Promise.all([
      db.query(
        'SELECT * FROM journal_entries WHERE campaign_id = $1 ORDER BY created_at',
        [campaignId]
      ),
      db.query(
        'SELECT * FROM quests WHERE campaign_id = $1 ORDER BY created_at',
        [campaignId]
      ),
      db.query(
        'SELECT * FROM npcs WHERE campaign_id = $1 ORDER BY name',
        [campaignId]
      ),
      db.query(
        `SELECT rl.formula, rl.result, rl.visibility, rl.rolled_at, u.username
         FROM roll_log rl
         LEFT JOIN users u ON rl.user_id = u.id
         WHERE rl.campaign_id = $1
         ORDER BY rl.rolled_at
         LIMIT 10000`,
        [campaignId]
      ),
    ]);

    const exportData = {
      version:    '1.0',
      exportedAt: new Date().toISOString(),
      exportedBy: req.user.username,
      campaign: {
        ...cam[0],
        journal:     journal.rows,
        quests:      quests.rows,
        npcs:        npcs.rows,
        rollHistory: rolls.rows,
      },
    };

    const json = JSON.stringify(exportData, null, 2);
    const filename = `${cam[0].title.replace(/[^a-z0-9]/gi, '_')}_export.json`;

    res.set({
      'Content-Type':        'application/json',
      'Content-Disposition': `attachment; filename="${filename}"`,
      'Content-Length':      Buffer.byteLength(json),
    });

    res.send(json);
  } catch (err) {
    next(err);
  }
});

// ═══════════════════════════════════════════════════════════════
// CAMPAIGN IMPORT
// ═══════════════════════════════════════════════════════════════

/**
 * POST /api/campaigns/import
 * Imports a campaign from a JSON export file.
 * Creates a new campaign (never overwrites existing).
 * Requires GM role.
 */
router.post('/import', authMiddleware, gmMiddleware, perUserStrictLimiter,
  async (req, res, next) => {
    try {
      const data = req.body;

      if (data.version !== '1.0') {
        return res.status(400).json({ error: 'Unsupported export version' });
      }

      const src      = data.campaign;
      const newId    = uuidv4();
      const newTitle = `${src.title} (Imported ${new Date().toLocaleDateString()})`;

      const client = await db.connect();
      try {
        await client.query('BEGIN');

        // Create campaign
        await client.query(
          `INSERT INTO campaigns (id, title, description, setting, system, gm_id, world_state, created_at, updated_at)
           VALUES ($1, $2, $3, $4, $5, $6, $7, NOW(), NOW())`,
          [newId, newTitle, src.description, src.setting, src.system,
           req.user.id, JSON.stringify(src.world_state || {})]
        );

        // Import journal entries
        for (const entry of src.journal || []) {
          await client.query(
            `INSERT INTO journal_entries (id, campaign_id, title, body, is_gm_only, created_at, updated_at)
             VALUES ($1, $2, $3, $4, $5, NOW(), NOW())`,
            [uuidv4(), newId, entry.title, entry.body, entry.is_gm_only || false]
          );
        }

        // Import quests
        for (const quest of src.quests || []) {
          await client.query(
            `INSERT INTO quests (id, campaign_id, title, description, is_complete, objectives, created_at)
             VALUES ($1, $2, $3, $4, $5, $6, NOW())`,
            [uuidv4(), newId, quest.title, quest.description,
             quest.is_complete || false, JSON.stringify(quest.objectives || [])]
          );
        }

        // Import NPCs
        for (const npc of src.npcs || []) {
          await client.query(
            `INSERT INTO npcs (id, campaign_id, name, description, location, attitude, notes, is_alive, created_at)
             VALUES ($1, $2, $3, $4, $5, $6, $7, $8, NOW())`,
            [uuidv4(), newId, npc.name, npc.description, npc.location,
             npc.attitude || 'neutral', JSON.stringify(npc.notes || {}), npc.is_alive !== false]
          );
        }

        await client.query('COMMIT');
      } catch (err) {
        await client.query('ROLLBACK');
        throw err;
      } finally {
        client.release();
      }

      const { rows } = await db.query(
        'SELECT * FROM campaigns WHERE id = $1', [newId]
      );

      res.status(201).json({
        campaign:        rows[0],
        importedJournal: (src.journal || []).length,
        importedQuests:  (src.quests  || []).length,
        importedNpcs:    (src.npcs    || []).length,
      });
    } catch (err) {
      next(err);
    }
  }
);

// ═══════════════════════════════════════════════════════════════
// BULK ASSET UPLOAD
// ═══════════════════════════════════════════════════════════════

/**
 * POST /api/campaigns/:id/assets/bulk
 * Upload a .zip of assets → extract → upload each file to S3.
 * Returns CDN URLs for all uploaded assets.
 * Limit: 50MB zip, 200 files per upload.
 */
router.post('/:id/assets/bulk', authMiddleware, perUserStrictLimiter,
  upload.single('archive'),
  async (req, res, next) => {
    try {
      if (!req.file) {
        return res.status(400).json({ error: 'No zip file provided' });
      }

      const campaignId = req.params.id;
      const folder     = req.body.folder || 'misc';

      const allowedFolders = ['maps', 'miniatures', 'audio', 'mods', 'portraits', 'misc'];
      if (!allowedFolders.includes(folder)) {
        return res.status(400).json({ error: 'Invalid folder' });
      }

      const allowedExtensions = new Set([
        '.png', '.jpg', '.jpeg', '.webp', '.gif',
        '.wav', '.ogg', '.mp3',
        '.gltf', '.glb', '.fbx', '.obj',
        '.lua', '.json',
      ]);

      const zip     = new AdmZip(req.file.buffer);
      const entries = zip.getEntries().filter(e => !e.isDirectory);

      if (entries.length > 200) {
        return res.status(400).json({ error: 'Zip contains too many files (max 200)' });
      }

      const uploaded = [];
      const skipped  = [];

      for (const entry of entries) {
        const ext = path.extname(entry.entryName).toLowerCase();
        if (!allowedExtensions.has(ext)) {
          skipped.push({ file: entry.entryName, reason: 'unsupported extension' });
          continue;
        }

        const fileBuffer  = entry.getData();
        const s3Key       = `${folder}/${req.user.id}/${campaignId}/${uuidv4()}-${path.basename(entry.entryName)}`;
        const contentType = mimeTypeFromExt(ext);

        try {
          await s3.send(new PutObjectCommand({
            Bucket:      BUCKET,
            Key:         s3Key,
            Body:        fileBuffer,
            ContentType: contentType,
            Metadata:    {
              uploadedBy:   req.user.id,
              campaignId,
              originalName: entry.entryName,
            },
          }));

          // Record in DB
          await db.query(
            `INSERT INTO assets (id, key, filename, content_type, folder, size_bytes, owner_id, campaign_id, created_at)
             VALUES ($1, $2, $3, $4, $5, $6, $7, $8, NOW())`,
            [uuidv4(), s3Key, path.basename(entry.entryName),
             contentType, folder, fileBuffer.length, req.user.id, campaignId]
          );

          uploaded.push({
            filename: entry.entryName,
            key:      s3Key,
            url:      `https://${process.env.DOMAIN}/assets/${s3Key}`,
            bytes:    fileBuffer.length,
          });
        } catch (uploadErr) {
          skipped.push({ file: entry.entryName, reason: uploadErr.message });
        }
      }

      res.status(201).json({
        uploaded: uploaded.length,
        skipped:  skipped.length,
        files:    uploaded,
        errors:   skipped,
      });
    } catch (err) {
      next(err);
    }
  }
);

// ─── Helper ────────────────────────────────────────────────────
function mimeTypeFromExt(ext) {
  const map = {
    '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
    '.webp': 'image/webp', '.gif': 'image/gif',
    '.wav': 'audio/wav', '.ogg': 'audio/ogg', '.mp3': 'audio/mpeg',
    '.gltf': 'model/gltf+json', '.glb': 'model/gltf-binary',
    '.fbx': 'application/octet-stream', '.obj': 'model/obj',
    '.lua': 'text/plain', '.json': 'application/json',
  };
  return map[ext] || 'application/octet-stream';
}

export default router;
