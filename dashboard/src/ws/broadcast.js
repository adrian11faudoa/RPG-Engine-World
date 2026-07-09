/**
 * dashboard/src/ws/broadcast.js
 * Cross-task WebSocket broadcasting via Redis Pub/Sub.
 *
 * Problem: ECS runs multiple Fargate tasks. A WebSocket connection
 * lives on exactly one task. Without pub/sub, a message sent to
 * Task 1 never reaches players connected to Task 2.
 *
 * Solution: Every broadcast publishes to Redis. Every task
 * subscribes and delivers to its locally-connected sockets.
 */

import Redis from 'ioredis';

const REDIS_URL = process.env.REDIS_URL;
const CHANNEL   = 'rf:ws:broadcast';

// Separate connections — ioredis subscriber cannot issue commands
const pub = new Redis(REDIS_URL, { lazyConnect: true });
const sub = new Redis(REDIS_URL, { lazyConnect: true });

// Map of sessionId → Set<WebSocket> (local to this task only)
const localConnections = new Map();

let initialized = false;

/**
 * Initialize pub/sub. Call once at server startup.
 * @param {Map} connectionsMap - the server's connections map
 */
export async function initBroadcast(connectionsMap) {
  await pub.connect();
  await sub.connect();

  // Copy reference so we deliver to local sockets
  Object.assign(localConnections, { _ref: connectionsMap });

  sub.subscribe(CHANNEL, (err) => {
    if (err) console.error('[WS] Redis sub error:', err.message);
    else console.log('[WS] Subscribed to Redis broadcast channel');
  });

  sub.on('message', (channel, raw) => {
    if (channel !== CHANNEL) return;
    let msg;
    try { msg = JSON.parse(raw); } catch { return; }

    const { sessionId, payload, excludeUserId } = msg;
    const local = connectionsMap.get(sessionId);
    if (!local) return;

    const data = JSON.stringify(payload);
    local.forEach(ws => {
      if (ws.userId !== excludeUserId && ws.readyState === 1 /* OPEN */) {
        ws.send(data);
      }
    });
  });

  initialized = true;
  console.log('[WS] Cross-task pub/sub initialized');
}

/**
 * Broadcast a message to all players in a session across ALL tasks.
 * @param {string} sessionId
 * @param {object} payload    - JSON-serializable message
 * @param {string} [excludeUserId] - skip this user (the sender)
 */
export function broadcast(sessionId, payload, excludeUserId = null) {
  const message = JSON.stringify({ sessionId, payload, excludeUserId });

  if (initialized) {
    pub.publish(CHANNEL, message);
  } else {
    // Fallback to local-only before Redis is ready
    const local = localConnections._ref?.get(sessionId);
    const data  = JSON.stringify(payload);
    local?.forEach(ws => {
      if (ws.userId !== excludeUserId && ws.readyState === 1) ws.send(data);
    });
  }
}

/**
 * Publish a system-level event visible across all sessions.
 * Used for: server maintenance notices, forced disconnects.
 */
export function broadcastGlobal(payload) {
  const message = JSON.stringify({ sessionId: '__global__', payload, excludeUserId: null });
  if (initialized) pub.publish(CHANNEL, message);
}

/**
 * Graceful shutdown — flush pending publishes before exit.
 */
export async function closeBroadcast() {
  await pub.quit();
  await sub.quit();
}
