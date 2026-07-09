/**
 * dashboard/src/middleware/auth.js
 * JWT authentication and role-based authorization middleware
 */

import jwt from 'jsonwebtoken';

const JWT_SECRET = process.env.JWT_SECRET;

/**
 * Verifies JWT from Authorization header.
 * Attaches decoded payload to req.user.
 */
export function authMiddleware(req, res, next) {
  const header = req.headers.authorization;
  if (!header || !header.startsWith('Bearer ')) {
    return res.status(401).json({ error: 'Authorization header missing or malformed' });
  }

  const token = header.slice(7);
  try {
    req.user = jwt.verify(token, JWT_SECRET);
    next();
  } catch (err) {
    const message = err.name === 'TokenExpiredError' ? 'Token expired' : 'Invalid token';
    return res.status(401).json({ error: message });
  }
}

/**
 * Requires GM or admin role.
 * Must be used after authMiddleware.
 */
export function gmMiddleware(req, res, next) {
  if (!req.user) return res.status(401).json({ error: 'Not authenticated' });
  if (req.user.role !== 'gm' && req.user.role !== 'admin') {
    return res.status(403).json({ error: 'Game Master role required' });
  }
  next();
}

/**
 * Requires admin role only.
 */
export function adminMiddleware(req, res, next) {
  if (!req.user) return res.status(401).json({ error: 'Not authenticated' });
  if (req.user.role !== 'admin') {
    return res.status(403).json({ error: 'Admin role required' });
  }
  next();
}

/**
 * Optional auth — attaches user if token present, but doesn't block.
 * Useful for endpoints that behave differently for authenticated users.
 */
export function optionalAuth(req, res, next) {
  const header = req.headers.authorization;
  if (header && header.startsWith('Bearer ')) {
    try {
      req.user = jwt.verify(header.slice(7), JWT_SECRET);
    } catch {
      // ignore invalid tokens for optional auth
    }
  }
  next();
}
