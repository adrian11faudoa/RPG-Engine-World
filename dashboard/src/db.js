import { Pool } from 'pg';
import Redis   from 'ioredis';

export const db = new Pool({
  connectionString:       process.env.DATABASE_URL,
  max:                    20,
  idleTimeoutMillis:      30_000,
  connectionTimeoutMillis: 5_000,
});

db.on('error', (err) => console.error('[DB] Pool error:', err.message));

export async function connectDB() {
  const client = await db.connect();
  await client.query('SELECT 1');
  client.release();
  console.log('[DB] PostgreSQL connected');
}

export const redis = new Redis(process.env.REDIS_URL, {
  retryStrategy:        (times) => Math.min(times * 100, 3000),
  maxRetriesPerRequest: 3,
  enableReadyCheck:     true,
  lazyConnect:          true,
});

redis.on('connect',      ()    => console.log('[Redis] Connected'));
redis.on('reconnecting', ()    => console.log('[Redis] Reconnecting...'));
redis.on('error',        (err) => console.error('[Redis] Error:', err.message));

export async function connectRedis() {
  await redis.connect();
}

export async function query(sql, params) {
  try {
    return await db.query(sql, params);
  } catch (err) {
    console.error('[DB] Query error:', err.message);
    throw err;
  }
}
