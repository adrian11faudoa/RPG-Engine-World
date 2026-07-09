-- migrations/002_indexes.sql
-- Performance indexes for high-traffic query patterns.
-- These run after 001_init.sql has created all tables.

-- ─── campaigns ───────────────────────────────────────────────
-- Dashboard: "my campaigns" sorted by recent activity
CREATE INDEX IF NOT EXISTS idx_campaigns_gm_updated
    ON campaigns(gm_id, updated_at DESC);

-- Full-text search on campaign title
CREATE INDEX IF NOT EXISTS idx_campaigns_title_trgm
    ON campaigns USING gin(title gin_trgm_ops);

-- ─── journal_entries ─────────────────────────────────────────
-- Campaign journal: paginate by date, filter gm_only
CREATE INDEX IF NOT EXISTS idx_journal_campaign_gm_date
    ON journal_entries(campaign_id, is_gm_only, created_at DESC);

-- ─── game_sessions ───────────────────────────────────────────
-- Admin dashboard: active sessions sorted by start time
CREATE INDEX IF NOT EXISTS idx_sessions_status_created
    ON game_sessions(status, created_at DESC);

-- Lookup by GameLift session ID (used on every player join)
CREATE UNIQUE INDEX IF NOT EXISTS idx_sessions_gamelift_id
    ON game_sessions(gamelift_session_id)
    WHERE gamelift_session_id IS NOT NULL;

-- ─── roll_log ────────────────────────────────────────────────
-- Session replay: all rolls in a session ordered by time
CREATE INDEX IF NOT EXISTS idx_rolls_session_time
    ON roll_log(session_id, rolled_at DESC);

-- Campaign roll history
CREATE INDEX IF NOT EXISTS idx_rolls_campaign_time
    ON roll_log(campaign_id, rolled_at DESC);

-- ─── assets ──────────────────────────────────────────────────
-- Asset browser: user's assets by folder and date
CREATE INDEX IF NOT EXISTS idx_assets_owner_folder_date
    ON assets(owner_id, folder, created_at DESC);

-- Campaign asset picker
CREATE INDEX IF NOT EXISTS idx_assets_campaign_folder
    ON assets(campaign_id, folder)
    WHERE campaign_id IS NOT NULL;

-- S3 key lookup (unique constraint also creates an index, but explicit for clarity)
CREATE UNIQUE INDEX IF NOT EXISTS idx_assets_key_unique
    ON assets(key);

-- ─── users ───────────────────────────────────────────────────
-- Admin user search by username
CREATE INDEX IF NOT EXISTS idx_users_username_lower
    ON users(lower(username));

-- Active users report
CREATE INDEX IF NOT EXISTS idx_users_last_login
    ON users(last_login DESC)
    WHERE last_login IS NOT NULL;

-- ─── npcs ────────────────────────────────────────────────────
-- NPC database: all alive NPCs in a campaign
CREATE INDEX IF NOT EXISTS idx_npcs_campaign_alive
    ON npcs(campaign_id, is_alive);

-- NPC name search
CREATE INDEX IF NOT EXISTS idx_npcs_name_trgm
    ON npcs USING gin(name gin_trgm_ops);

-- ─── quests ──────────────────────────────────────────────────
-- Active quests per campaign
CREATE INDEX IF NOT EXISTS idx_quests_campaign_active
    ON quests(campaign_id, is_complete);

-- ─── Enable pg_trgm for full-text search (if not already) ────
CREATE EXTENSION IF NOT EXISTS pg_trgm;
