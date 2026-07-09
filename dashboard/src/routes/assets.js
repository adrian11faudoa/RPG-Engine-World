/**
 * dashboard/src/routes/assets.js
 * S3 asset management via presigned URLs
 */

import { Router } from 'express';
import { v4 as uuidv4 } from 'uuid';
import {
  S3Client,
  PutObjectCommand,
  DeleteObjectCommand,
  HeadObjectCommand,
  ListObjectsV2Command,
} from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';
import { db }           from '../db.js';
import { authMiddleware } from '../middleware/auth.js';

const router = Router();
router.use(authMiddleware);

const s3 = new S3Client({ region: process.env.AWS_REGION || 'us-east-1' });
const BUCKET = process.env.ASSETS_BUCKET;
const DOMAIN  = process.env.DOMAIN || 'localhost:3000';
const CDN_BASE = `https://${DOMAIN}/assets`;

const ALLOWED_FOLDERS = new Set(['maps', 'miniatures', 'audio', 'mods', 'portraits', 'misc']);

const ALLOWED_CONTENT_TYPES = new Set([
  'image/png', 'image/jpeg', 'image/webp', 'image/gif',
  'audio/ogg', 'audio/wav', 'audio/mpeg',
  'application/octet-stream',
  'model/gltf-binary',
  'application/zip',
  'application/json',
]);

const MAX_FILE_SIZES = {
  maps:        50 * 1024 * 1024,   // 50 MB
  miniatures:  20 * 1024 * 1024,   // 20 MB
  audio:       30 * 1024 * 1024,   // 30 MB
  mods:        100 * 1024 * 1024,  // 100 MB
  portraits:   5  * 1024 * 1024,   // 5 MB
  misc:        20 * 1024 * 1024,   // 20 MB
};

// ─── GET /api/assets ──────────────────────────────────────────
router.get('/', async (req, res, next) => {
  try {
    const { folder, campaign_id, limit = '50', offset = '0' } = req.query;
    const params = [req.user.id];
    let sql = `SELECT id, key, filename, content_type, folder, size_bytes,
                      campaign_id, is_public, created_at,
                      concat('${CDN_BASE}/', key) AS url
               FROM assets
               WHERE (owner_id = $1 OR is_public = true)`;

    if (folder) { params.push(folder); sql += ` AND folder = $${params.length}`; }
    if (campaign_id) { params.push(campaign_id); sql += ` AND campaign_id = $${params.length}`; }

    sql += ` ORDER BY created_at DESC LIMIT $${params.length + 1} OFFSET $${params.length + 2}`;
    params.push(Math.min(parseInt(limit), 200), parseInt(offset));

    const { rows } = await db.query(sql, params);
    res.json(rows);
  } catch (err) { next(err); }
});

// ─── POST /api/assets/upload-url ─────────────────────────────
router.post('/upload-url', async (req, res, next) => {
  try {
    const { filename, contentType, folder = 'misc', campaignId, isPublic = false, fileSizeBytes } = req.body;

    if (!filename || !contentType) {
      return res.status(400).json({ error: 'filename and contentType are required' });
    }
    if (!ALLOWED_FOLDERS.has(folder)) {
      return res.status(400).json({ error: `Invalid folder. Allowed: ${[...ALLOWED_FOLDERS].join(', ')}` });
    }
    if (!ALLOWED_CONTENT_TYPES.has(contentType)) {
      return res.status(400).json({ error: 'File type not permitted' });
    }
    const maxSize = MAX_FILE_SIZES[folder];
    if (fileSizeBytes && fileSizeBytes > maxSize) {
      return res.status(413).json({ error: `File too large. Max ${maxSize / 1024 / 1024}MB for ${folder}` });
    }

    // Sanitize filename
    const safeName = filename.replace(/[^a-zA-Z0-9._\-]/g, '_').slice(0, 200);
    const key = `${folder}/${req.user.id}/${uuidv4()}-${safeName}`;

    const putCmd = new PutObjectCommand({
      Bucket:       BUCKET,
      Key:          key,
      ContentType:  contentType,
      Metadata: {
        uploadedBy:   req.user.id,
        originalName: safeName,
        folder,
      },
    });

    const uploadUrl = await getSignedUrl(s3, putCmd, { expiresIn: 300 }); // 5-min window

    // Pre-register in DB (will be confirmed on successful upload)
    const assetId = uuidv4();
    await db.query(
      `INSERT INTO assets (id, key, filename, content_type, folder, size_bytes, owner_id, campaign_id, is_public, created_at)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, NOW())`,
      [assetId, key, safeName, contentType, folder, fileSizeBytes || null,
       req.user.id, campaignId || null, isPublic]
    );

    res.json({
      assetId,
      uploadUrl,
      key,
      url: `${CDN_BASE}/${key}`,
      expiresIn: 300,
    });
  } catch (err) { next(err); }
});

// ─── PATCH /api/assets/:id  (confirm upload / update metadata) ─
router.patch('/:id', async (req, res, next) => {
  try {
    const { is_public, campaign_id, size_bytes } = req.body;

    // Verify the object actually landed in S3
    const { rows: [asset] } = await db.query(
      'SELECT key FROM assets WHERE id = $1 AND owner_id = $2',
      [req.params.id, req.user.id]
    );
    if (!asset) return res.status(404).json({ error: 'Asset not found' });

    try {
      await s3.send(new HeadObjectCommand({ Bucket: BUCKET, Key: asset.key }));
    } catch {
      return res.status(422).json({ error: 'Asset not yet uploaded to S3' });
    }

    const { rows } = await db.query(
      `UPDATE assets SET
         is_public   = COALESCE($1, is_public),
         campaign_id = COALESCE($2, campaign_id),
         size_bytes  = COALESCE($3, size_bytes)
       WHERE id = $4 RETURNING *, concat('${CDN_BASE}/', key) AS url`,
      [is_public, campaign_id, size_bytes, req.params.id]
    );
    res.json(rows[0]);
  } catch (err) { next(err); }
});

// ─── DELETE /api/assets/:id ───────────────────────────────────
router.delete('/:id', async (req, res, next) => {
  try {
    const { rows } = await db.query(
      'SELECT key FROM assets WHERE id = $1 AND owner_id = $2',
      [req.params.id, req.user.id]
    );
    if (!rows[0]) return res.status(404).json({ error: 'Asset not found' });

    // Delete from S3
    await s3.send(new DeleteObjectCommand({ Bucket: BUCKET, Key: rows[0].key }));

    // Delete from DB
    await db.query('DELETE FROM assets WHERE id = $1', [req.params.id]);
    res.json({ success: true });
  } catch (err) { next(err); }
});

// ─── GET /api/assets/browse/:folder  (list S3 folder directly) ─
router.get('/browse/:folder', async (req, res, next) => {
  try {
    if (!ALLOWED_FOLDERS.has(req.params.folder)) {
      return res.status(400).json({ error: 'Invalid folder' });
    }
    const cmd = new ListObjectsV2Command({
      Bucket: BUCKET,
      Prefix: `${req.params.folder}/${req.user.id}/`,
      MaxKeys: 100,
    });
    const { Contents = [] } = await s3.send(cmd);
    res.json(Contents.map(obj => ({
      key:          obj.Key,
      url:          `${CDN_BASE}/${obj.Key}`,
      size:         obj.Size,
      lastModified: obj.LastModified,
    })));
  } catch (err) { next(err); }
});

export default router;
