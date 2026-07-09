/**
 * dashboard/src/middleware/rateLimiter.js
 * Per-user rate limiting backed by Redis.
 * Falls back to IP-based limiting if user is not authenticated.
 *
 * Usage:
 *   import { perUserLimiter, perUserStrictLimiter } from '../middleware/rateLimiter.js';
 *   router.post('/create', authMiddleware, perUserStrictLimiter, handler);
 */

import { redis } from '../db.js';

/**
 * Creates a Redis-backed rate limiter middleware.
 * @param {object} opts
 * @param {number}  opts.windowMs     - Time window in ms
 * @param {number}  opts.max          - Max requests per window
 * @param {string}  opts.keyPrefix    - Redis key prefix (e.g. 'rl:dice')
 * @param {string}  opts.message      - Error message when limit exceeded
 */
function createLimiter({ windowMs, max, keyPrefix, message }) {
  const windowSecs = Math.ceil(windowMs / 1000);

  return async function rateLimiterMiddleware(req, res, next) {
    // Key: per-user if authenticated, per-IP otherwise
    const identifier = req.user?.id || req.ip;
    const key = `${keyPrefix}:${identifier}`;

    try {
      const pipe = redis.pipeline();
      pipe.incr(key);
      pipe.expire(key, windowSecs);
      const [[, count]] = await pipe.exec();

      // Expose headers for client-side backoff
      const remaining = Math.max(0, max - count);
      const resetAt   = Date.now() + windowMs;

      res.set({
        'X-RateLimit-Limit':     max,
        'X-RateLimit-Remaining': remaining,
        'X-RateLimit-Reset':     Math.ceil(resetAt / 1000),
      });

      if (count > max) {
        return res.status(429).json({
          error:   message || 'Too many requests',
          retryAfter: windowSecs,
        });
      }

      next();
    } catch (err) {
      // Redis down — fail open so game sessions aren't interrupted
      console.error('[RateLimit] Redis error, failing open:', err.message);
      next();
    }
  };
}

/** Standard per-user limiter: 120 requests / 1 minute */
export const perUserLimiter = createLimiter({
  windowMs:  60 * 1000,
  max:       120,
  keyPrefix: 'rl:user',
  message:   'Request rate limit exceeded. Please slow down.',
});

/** Strict per-user limiter for expensive operations (session create, bulk upload): 10 / minute */
export const perUserStrictLimiter = createLimiter({
  windowMs:  60 * 1000,
  max:       10,
  keyPrefix: 'rl:strict',
  message:   'Operation rate limit exceeded. Please wait before trying again.',
});

/** Dice roll limiter: 60 rolls / minute (prevents roll-spam) */
export const diceRollLimiter = createLimiter({
  windowMs:  60 * 1000,
  max:       60,
  keyPrefix: 'rl:dice',
  message:   'Dice roll rate limit exceeded.',
});

/** Auth limiter: 10 attempts / 15 minutes (brute-force protection) */
export const authAttemptLimiter = createLimiter({
  windowMs:  15 * 60 * 1000,
  max:       10,
  keyPrefix: 'rl:auth',
  message:   'Too many authentication attempts. Please wait 15 minutes.',
});
