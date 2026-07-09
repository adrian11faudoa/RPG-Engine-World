/**
 * dashboard/src/__tests__/auth.test.js
 * Integration tests for authentication routes.
 * Run: npm test
 */

import request from 'supertest';
import app     from '../server.js';
import { db }  from '../db.js';

const TEST_USER = {
  username: `testuser_${Date.now()}`,
  email:    `test_${Date.now()}@example.com`,
  password: 'TestPassword123!',
};

let authToken = null;

afterAll(async () => {
  // Clean up test user
  await db.query('DELETE FROM users WHERE email = $1', [TEST_USER.email]);
  await db.end();
});

// ─── Registration ──────────────────────────────────────────────
describe('POST /api/auth/register', () => {
  it('creates a new user and returns a JWT', async () => {
    const res = await request(app)
      .post('/api/auth/register')
      .send(TEST_USER)
      .expect(201);

    expect(res.body).toHaveProperty('token');
    expect(res.body.user.username).toBe(TEST_USER.username);
    expect(res.body.user.role).toBe('player');
    expect(res.body.user).not.toHaveProperty('password_hash');

    authToken = res.body.token;
  });

  it('rejects duplicate email', async () => {
    const res = await request(app)
      .post('/api/auth/register')
      .send(TEST_USER)
      .expect(409);

    expect(res.body.error).toMatch(/taken/i);
  });

  it('rejects missing fields', async () => {
    await request(app)
      .post('/api/auth/register')
      .send({ username: 'incomplete' })
      .expect(400);
  });
});

// ─── Login ─────────────────────────────────────────────────────
describe('POST /api/auth/login', () => {
  it('returns a JWT for valid credentials', async () => {
    const res = await request(app)
      .post('/api/auth/login')
      .send({ email: TEST_USER.email, password: TEST_USER.password })
      .expect(200);

    expect(res.body).toHaveProperty('token');
    expect(res.body.user.email).toBe(TEST_USER.email);
  });

  it('rejects wrong password', async () => {
    const res = await request(app)
      .post('/api/auth/login')
      .send({ email: TEST_USER.email, password: 'wrongpassword' })
      .expect(401);

    expect(res.body.error).toMatch(/invalid/i);
  });

  it('rejects unknown email', async () => {
    await request(app)
      .post('/api/auth/login')
      .send({ email: 'nobody@nowhere.com', password: 'anything' })
      .expect(401);
  });
});

// ─── Protected Route ───────────────────────────────────────────
describe('GET /api/auth/me', () => {
  it('returns current user for valid token', async () => {
    const res = await request(app)
      .get('/api/auth/me')
      .set('Authorization', `Bearer ${authToken}`)
      .expect(200);

    expect(res.body.username).toBe(TEST_USER.username);
    expect(res.body).not.toHaveProperty('password_hash');
  });

  it('returns 401 without token', async () => {
    await request(app)
      .get('/api/auth/me')
      .expect(401);
  });

  it('returns 401 with invalid token', async () => {
    await request(app)
      .get('/api/auth/me')
      .set('Authorization', 'Bearer invalid.token.here')
      .expect(401);
  });
});

// ─── Health Check ──────────────────────────────────────────────
describe('GET /health', () => {
  it('returns ok', async () => {
    const res = await request(app)
      .get('/health')
      .expect(200);

    expect(res.body.status).toBe('ok');
    expect(res.body).toHaveProperty('timestamp');
  });
});
