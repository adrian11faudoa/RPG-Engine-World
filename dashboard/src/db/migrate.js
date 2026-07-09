/**
 * dashboard/src/db/migrate.js
 * Simple sequential migration runner.
 * Usage:
 *   node src/db/migrate.js          # run all pending migrations
 *   node src/db/migrate.js rollback # roll back last migration
 *
 * Each migration file exports { up, down } async functions.
 */

import { db }    from '../db.js';
import fs        from 'fs';
import path      from 'path';
import { fileURLToPath } from 'url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const MIGRATIONS_DIR = path.join(__dirname, 'migrations');

// ─── Ensure migrations tracking table exists ───────────────────
async function ensureMigrationsTable() {
  await db.query(`
    CREATE TABLE IF NOT EXISTS _migrations (
      id          SERIAL PRIMARY KEY,
      filename    TEXT UNIQUE NOT NULL,
      applied_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
    )
  `);
}

// ─── Get applied migrations ────────────────────────────────────
async function getApplied() {
  const { rows } = await db.query('SELECT filename FROM _migrations ORDER BY id');
  return new Set(rows.map(r => r.filename));
}

// ─── Run all pending migrations ────────────────────────────────
async function migrate() {
  await ensureMigrationsTable();
  const applied = await getApplied();

  // Read and sort migration files (001_init.sql, 002_add_quests.sql, …)
  const files = fs.readdirSync(MIGRATIONS_DIR)
    .filter(f => f.endsWith('.sql'))
    .sort();

  const pending = files.filter(f => !applied.has(f));

  if (pending.length === 0) {
    console.log('[Migrate] ✅ All migrations applied — nothing to do.');
    return;
  }

  for (const filename of pending) {
    const sql = fs.readFileSync(path.join(MIGRATIONS_DIR, filename), 'utf8');
    console.log(`[Migrate] Applying: ${filename}…`);

    const client = await db.connect();
    try {
      await client.query('BEGIN');
      await client.query(sql);
      await client.query('INSERT INTO _migrations (filename) VALUES ($1)', [filename]);
      await client.query('COMMIT');
      console.log(`[Migrate] ✅ ${filename}`);
    } catch (err) {
      await client.query('ROLLBACK');
      console.error(`[Migrate] ❌ Failed: ${filename}`, err.message);
      process.exit(1);
    } finally {
      client.release();
    }
  }

  console.log(`[Migrate] Done — ${pending.length} migration(s) applied.`);
}

// ─── Rollback last migration ───────────────────────────────────
async function rollback() {
  await ensureMigrationsTable();
  const { rows } = await db.query(
    'SELECT filename FROM _migrations ORDER BY id DESC LIMIT 1'
  );

  if (rows.length === 0) {
    console.log('[Migrate] Nothing to roll back.');
    return;
  }

  const { filename } = rows[0];
  const rollbackPath = path.join(MIGRATIONS_DIR, filename.replace('.sql', '.rollback.sql'));

  if (!fs.existsSync(rollbackPath)) {
    console.error(`[Migrate] ❌ No rollback file found: ${rollbackPath}`);
    process.exit(1);
  }

  const sql = fs.readFileSync(rollbackPath, 'utf8');
  console.log(`[Migrate] Rolling back: ${filename}…`);

  const client = await db.connect();
  try {
    await client.query('BEGIN');
    await client.query(sql);
    await client.query('DELETE FROM _migrations WHERE filename = $1', [filename]);
    await client.query('COMMIT');
    console.log(`[Migrate] ✅ Rolled back: ${filename}`);
  } catch (err) {
    await client.query('ROLLBACK');
    console.error(`[Migrate] ❌ Rollback failed:`, err.message);
    process.exit(1);
  } finally {
    client.release();
  }
}

// ─── Entry point ──────────────────────────────────────────────
const command = process.argv[2];

if (command === 'rollback') {
  rollback().then(() => process.exit(0)).catch(err => {
    console.error(err);
    process.exit(1);
  });
} else {
  migrate().then(() => process.exit(0)).catch(err => {
    console.error(err);
    process.exit(1);
  });
}
