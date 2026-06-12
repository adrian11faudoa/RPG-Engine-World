/**
 * RealmForge Engine — AWS Client SDK
 * Runs in the game client (Electron/browser companion or as a JS bridge)
 * Handles: session creation, joining, WebSocket relay, S3 asset downloads
 */

// ─── Config (injected at build time or fetched from /api/config) ─
const CONFIG = {
  apiBase:   'https://realmforge.gg/api',
  wsUrl:     'wss://realmforge.gg/ws',
  assetsBase:'https://realmforge.gg/assets',
};

// ─── Auth Token Storage ────────────────────────────────────────
let authToken = null;
let currentUser = null;

export function setAuthToken(token) {
  authToken = token;
  try { localStorage.setItem('rf_token', token); } catch {}
}

export function loadStoredToken() {
  try { authToken = localStorage.getItem('rf_token'); } catch {}
  return authToken;
}

function authHeaders() {
  return {
    'Content-Type': 'application/json',
    ...(authToken ? { Authorization: `Bearer ${authToken}` } : {}),
  };
}

async function apiFetch(path, options = {}) {
  const res = await fetch(`${CONFIG.apiBase}${path}`, {
    ...options,
    headers: { ...authHeaders(), ...(options.headers || {}) },
  });

  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw Object.assign(new Error(err.error || 'API error'), { status: res.status, data: err });
  }

  return res.json();
}

// ═══════════════════════════════════════════════════════════════
// AUTH
// ═══════════════════════════════════════════════════════════════

export async function register(username, email, password) {
  const { token, user } = await apiFetch('/auth/register', {
    method: 'POST',
    body: JSON.stringify({ username, email, password }),
  });
  setAuthToken(token);
  currentUser = user;
  return user;
}

export async function login(email, password) {
  const { token, user } = await apiFetch('/auth/login', {
    method: 'POST',
    body: JSON.stringify({ email, password }),
  });
  setAuthToken(token);
  currentUser = user;
  return user;
}

export function logout() {
  authToken = null;
  currentUser = null;
  try { localStorage.removeItem('rf_token'); } catch {}
}

export async function getMe() {
  currentUser = await apiFetch('/auth/me');
  return currentUser;
}

// ═══════════════════════════════════════════════════════════════
// CAMPAIGNS
// ═══════════════════════════════════════════════════════════════

export const campaigns = {
  list:   ()              => apiFetch('/campaigns'),
  get:    (id)            => apiFetch(`/campaigns/${id}`),
  create: (data)          => apiFetch('/campaigns', { method: 'POST', body: JSON.stringify(data) }),
  update: (id, data)      => apiFetch(`/campaigns/${id}`, { method: 'PATCH', body: JSON.stringify(data) }),

  journal: {
    list:   (campaignId)  => apiFetch(`/campaigns/${campaignId}/journal`),
    add:    (campaignId, entry) =>
      apiFetch(`/campaigns/${campaignId}/journal`, { method: 'POST', body: JSON.stringify(entry) }),
  },
};

// ═══════════════════════════════════════════════════════════════
// GAME SESSIONS (GameLift)
// ═══════════════════════════════════════════════════════════════

export const sessions = {

  /**
   * GM creates a new game session → GameLift spins up server
   * Returns { gameSessionId, playerSessionId, serverEndpoint }
   */
  async create(campaignId, options = {}) {
    const result = await apiFetch('/sessions/create', {
      method: 'POST',
      body: JSON.stringify({ campaignId, maxPlayers: options.maxPlayers || 8, ...options }),
    });
    return result;
  },

  /**
   * Player joins an existing session
   * Returns { playerSessionId, serverEndpoint }
   */
  async join(gameSessionId) {
    return apiFetch(`/sessions/${gameSessionId}/join`, { method: 'POST' });
  },

  /**
   * List active sessions available to join
   */
  async listActive() {
    return apiFetch('/sessions/active');
  },
};

// ═══════════════════════════════════════════════════════════════
// ASSETS (S3 via presigned URLs)
// ═══════════════════════════════════════════════════════════════

export const assets = {

  async list(folder) {
    return apiFetch(`/assets${folder ? `?folder=${folder}` : ''}`);
  },

  /**
   * Upload a file to S3 via presigned URL.
   * @param {File} file - Browser File object
   * @param {string} folder - 'maps' | 'miniatures' | 'audio' | 'mods' | 'portraits'
   * @param {function} onProgress - progress callback (0-100)
   */
  async upload(file, folder = 'misc', onProgress) {
    // 1. Get presigned URL from API
    const { uploadUrl, key, cdnUrl } = await apiFetch('/assets/upload-url', {
      method: 'POST',
      body: JSON.stringify({ filename: file.name, contentType: file.type, folder }),
    });

    // 2. Upload directly to S3 using presigned URL (bypasses API server)
    await new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open('PUT', uploadUrl);
      xhr.setRequestHeader('Content-Type', file.type);

      if (onProgress) {
        xhr.upload.addEventListener('progress', (e) => {
          if (e.lengthComputable) onProgress(Math.round((e.loaded / e.total) * 100));
        });
      }

      xhr.onload  = () => (xhr.status === 200 ? resolve() : reject(new Error(`S3 upload failed: ${xhr.status}`)));
      xhr.onerror = () => reject(new Error('S3 upload network error'));
      xhr.send(file);
    });

    return { key, cdnUrl };
  },

  /**
   * Get a CDN URL for an asset key
   */
  url(key) {
    return `${CONFIG.assetsBase}/${key}`;
  },

  async delete(assetId) {
    return apiFetch(`/assets/${assetId}`, { method: 'DELETE' });
  },
};

// ═══════════════════════════════════════════════════════════════
// WEBSOCKET — Real-time session relay
# (dice, fog, initiative, chat — mirrors the UE5 session state)
// ═══════════════════════════════════════════════════════════════

export class RealmForgeSocket {
  constructor() {
    this.ws        = null;
    this.sessionId = null;
    this.listeners = new Map();
    this.reconnectAttempts = 0;
    this.maxReconnects = 10;
    this.pingInterval  = null;
  }

  /**
   * Connect to the WebSocket relay server and authenticate
   */
  async connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(CONFIG.wsUrl);

      this.ws.onopen = () => {
        // Authenticate immediately on connect
        this._send({ type: 'auth', token: authToken });
        this.reconnectAttempts = 0;

        // Keepalive ping every 30s
        this.pingInterval = setInterval(() => {
          this._send({ type: 'ping' });
        }, 30000);
      };

      this.ws.onmessage = (event) => {
        let msg;
        try { msg = JSON.parse(event.data); } catch { return; }

        if (msg.type === 'auth_ok') {
          resolve(this);
        } else if (msg.type === 'auth_error') {
          reject(new Error('WebSocket auth failed'));
        }

        // Dispatch to listeners
        this._emit(msg.type, msg);
        this._emit('*', msg);  // wildcard listener
      };

      this.ws.onclose = (event) => {
        clearInterval(this.pingInterval);
        this._emit('disconnected', { code: event.code, reason: event.reason });
        this._scheduleReconnect();
      };

      this.ws.onerror = (err) => {
        this._emit('error', err);
        reject(err);
      };
    });
  }

  /**
   * Join a specific game session relay channel
   */
  joinSession(sessionId) {
    this.sessionId = sessionId;
    this._send({ type: 'join_session', sessionId });
  }

  // ─── Game Events ─────────────────────────────────────────────

  sendDiceRoll(formula, result, visibility = 'public') {
    this._send({ type: 'dice_roll', formula, result, visibility });
  }

  sendFogUpdate(fogData) {
    this._send({ type: 'fog_update', fogData });
  }

  sendInitiativeUpdate(initiative) {
    this._send({ type: 'initiative_update', initiative });
  }

  sendChat(text, style = 'normal') {
    this._send({ type: 'chat_message', text, style });
  }

  sendMiniatureMove(miniatureName, from, to) {
    this._send({ type: 'miniature_move', miniatureName, from, to });
  }

  // ─── Event Listeners ──────────────────────────────────────────

  on(eventType, callback) {
    if (!this.listeners.has(eventType)) this.listeners.set(eventType, new Set());
    this.listeners.get(eventType).add(callback);
    return () => this.off(eventType, callback);  // returns unsubscribe fn
  }

  off(eventType, callback) {
    this.listeners.get(eventType)?.delete(callback);
  }

  once(eventType, callback) {
    const wrapper = (data) => {
      callback(data);
      this.off(eventType, wrapper);
    };
    this.on(eventType, wrapper);
  }

  // ─── Disconnect ───────────────────────────────────────────────

  disconnect() {
    this.maxReconnects = 0;  // prevent auto-reconnect
    clearInterval(this.pingInterval);
    this.ws?.close(1000, 'Client disconnect');
  }

  // ─── Internals ───────────────────────────────────────────────

  _send(data) {
    if (this.ws?.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(data));
    }
  }

  _emit(type, data) {
    this.listeners.get(type)?.forEach(cb => {
      try { cb(data); } catch (e) { console.error('[WS] Listener error:', e); }
    });
  }

  async _scheduleReconnect() {
    if (this.reconnectAttempts >= this.maxReconnects) return;
    this.reconnectAttempts++;
    const delay = Math.min(1000 * 2 ** this.reconnectAttempts, 30000);
    console.log(`[WS] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})`);
    await new Promise(r => setTimeout(r, delay));
    try {
      await this.connect();
      if (this.sessionId) this.joinSession(this.sessionId);
      this._emit('reconnected', {});
    } catch {
      this._scheduleReconnect();
    }
  }
}

// ─── Singleton socket instance ─────────────────────────────────
export const socket = new RealmForgeSocket();

// ═══════════════════════════════════════════════════════════════
// UE5 BRIDGE (sends events from JS → UE5 via Pixel Streaming
//             or Unreal's custom HTML overlay JS bridge)
// ═══════════════════════════════════════════════════════════════

export const ue5Bridge = {
  /**
   * Send a message to UE5 via the Unreal JS Bridge
   * UE5 side: WebBrowserWidget.BindUFunction / UE5.OnReceivedEvent
   */
  send(eventName, data) {
    if (window.ue && window.ue.interface) {
      // UE4/5 browser widget bridge
      window.ue.interface.broadcast(eventName, JSON.stringify(data));
    } else {
      // Fallback: postMessage for Electron/iframe
      window.parent?.postMessage({ ue5: true, eventName, data }, '*');
    }
  },

  /**
   * Register a handler for events FROM UE5
   */
  on(eventName, callback) {
    window.addEventListener('message', (event) => {
      if (event.data?.ue5Bridge && event.data.eventName === eventName) {
        callback(event.data.data);
      }
    });
  },

  // Convenience methods
  rollDice:       (formula, result)   => ue5Bridge.send('dice_rolled', { formula, result }),
  moveMiniature:  (name, location)    => ue5Bridge.send('miniature_moved', { name, location }),
  selectMiniature:(name)              => ue5Bridge.send('miniature_selected', { name }),
  updateFog:      (fogData)           => ue5Bridge.send('fog_updated', { fogData }),
  switchScene:    (mapName)           => ue5Bridge.send('scene_switch', { mapName }),
  showNotification:(msg, style)       => ue5Bridge.send('notification', { msg, style }),
};

// ═══════════════════════════════════════════════════════════════
// USAGE EXAMPLE
// ═══════════════════════════════════════════════════════════════
/*
import { login, sessions, assets, socket, ue5Bridge } from './rf-aws-client.js';

// 1. Login
const user = await login('gm@example.com', 'password');

// 2. Create game session (GM)
const { gameSessionId, serverEndpoint } = await sessions.create('campaign-uuid-here');
console.log('Connect UE5 client to:', serverEndpoint);

// 3. Connect WebSocket
await socket.connect();
socket.joinSession(gameSessionId);

// 4. Listen for events from other players
socket.on('dice_roll', ({ username, formula, result }) => {
  console.log(`${username} rolled ${formula} = ${result.total}`);
  ue5Bridge.rollDice(formula, result);
});

socket.on('miniature_move', ({ miniatureName, to }) => {
  ue5Bridge.moveMiniature(miniatureName, to);
});

// 5. Upload a custom map
const file = document.querySelector('input[type=file]').files[0];
const { cdnUrl } = await assets.upload(file, 'maps', (pct) => console.log(`${pct}%`));
console.log('Map available at:', cdnUrl);
*/
