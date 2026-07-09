-- migrations/001_init.sql
-- Initial RealmForge schema — creates all core tables.
-- Applied by: node src/db/migrate.js

-- ─── Extensions ──────────────────────────────────────────────
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
CREATE EXTENSION IF NOT EXISTS "pg_stat_statements";

-- ─── Users ───────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS users (
    id            UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    username      TEXT UNIQUE NOT NULL,
    email         TEXT UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    role          TEXT NOT NULL DEFAULT 'player'
                  CHECK (role IN ('player', 'gm', 'admin')),
    avatar_key    TEXT,
    created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    last_login    TIMESTAMPTZ
);

-- ─── Campaigns ───────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS campaigns (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    title       TEXT NOT NULL,
    description TEXT,
    setting     TEXT DEFAULT 'Fantasy',
    system      TEXT DEFAULT 'D&D 5e',
    gm_id       UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    active_map  TEXT,
    world_state JSONB NOT NULL DEFAULT '{}',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_campaigns_gm_id  ON campaigns(gm_id);
CREATE INDEX idx_campaigns_updated ON campaigns(updated_at DESC);

-- ─── Campaign Players ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS campaign_players (
    campaign_id UUID REFERENCES campaigns(id) ON DELETE CASCADE,
    user_id     UUID REFERENCES users(id)     ON DELETE CASCADE,
    role        TEXT DEFAULT 'player'
                CHECK (role IN ('player', 'assistant_gm', 'spectator')),
    joined_at   TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (campaign_id, user_id)
);

-- ─── Journal Entries ─────────────────────────────────────────
CREATE TABLE IF NOT EXISTS journal_entries (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    campaign_id UUID NOT NULL REFERENCES campaigns(id) ON DELETE CASCADE,
    title       TEXT NOT NULL,
    body        TEXT,
    author_id   UUID REFERENCES users(id),
    is_gm_only  BOOLEAN NOT NULL DEFAULT false,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_journal_campaign ON journal_entries(campaign_id);

-- ─── Quests ──────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS quests (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    campaign_id UUID NOT NULL REFERENCES campaigns(id) ON DELETE CASCADE,
    title       TEXT NOT NULL,
    description TEXT,
    is_complete BOOLEAN NOT NULL DEFAULT false,
    objectives  JSONB NOT NULL DEFAULT '[]',
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- ─── NPCs ────────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS npcs (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    campaign_id UUID NOT NULL REFERENCES campaigns(id) ON DELETE CASCADE,
    name        TEXT NOT NULL,
    description TEXT,
    location    TEXT,
    attitude    TEXT DEFAULT 'neutral'
                CHECK (attitude IN ('friendly', 'neutral', 'hostile', 'unknown')),
    notes       JSONB NOT NULL DEFAULT '{}',
    is_alive    BOOLEAN NOT NULL DEFAULT true,
    portrait_key TEXT,
    created_at  TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_npcs_campaign ON npcs(campaign_id);

-- ─── Game Sessions ───────────────────────────────────────────
CREATE TABLE IF NOT EXISTS game_sessions (
    id                  UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    campaign_id         UUID REFERENCES campaigns(id) ON DELETE SET NULL,
    gamelift_session_id TEXT UNIQUE,
    status              TEXT DEFAULT 'ACTIVE'
                        CHECK (status IN ('ACTIVE', 'ENDED', 'ERROR')),
    gm_id               UUID REFERENCES users(id),
    player_count        INT DEFAULT 0,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    ended_at            TIMESTAMPTZ
);

CREATE INDEX idx_sessions_campaign ON game_sessions(campaign_id);
CREATE INDEX idx_sessions_status   ON game_sessions(status);

-- ─── Roll Log ────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS roll_log (
    id          UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    session_id  TEXT,
    campaign_id UUID REFERENCES campaigns(id) ON DELETE CASCADE,
    user_id     UUID REFERENCES users(id),
    formula     TEXT NOT NULL,
    result      JSONB NOT NULL,
    visibility  TEXT DEFAULT 'public'
                CHECK (visibility IN ('public', 'gm_only', 'private')),
    rolled_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_rolls_session  ON roll_log(session_id);
CREATE INDEX idx_rolls_campaign ON roll_log(campaign_id);

-- ─── Assets ──────────────────────────────────────────────────
CREATE TABLE IF NOT EXISTS assets (
    id           UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    key          TEXT NOT NULL UNIQUE,
    filename     TEXT,
    content_type TEXT,
    folder       TEXT,
    size_bytes   BIGINT,
    owner_id     UUID REFERENCES users(id) ON DELETE SET NULL,
    campaign_id  UUID REFERENCES campaigns(id) ON DELETE SET NULL,
    is_public    BOOLEAN NOT NULL DEFAULT false,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_assets_owner    ON assets(owner_id);
CREATE INDEX idx_assets_campaign ON assets(campaign_id);
CREATE INDEX idx_assets_folder   ON assets(folder);

-- ─── Triggers: auto-update updated_at ────────────────────────
CREATE OR REPLACE FUNCTION set_updated_at()
RETURNS TRIGGER AS $$
BEGIN NEW.updated_at = NOW(); RETURN NEW; END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER campaigns_updated_at
    BEFORE UPDATE ON campaigns
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

CREATE TRIGGER journal_updated_at
    BEFORE UPDATE ON journal_entries
    FOR EACH ROW EXECUTE FUNCTION set_updated_at();

-- ─── Seed: default admin (change password on first login!) ────
INSERT INTO users (username, email, password_hash, role)
VALUES (
    'admin',
    'admin@realmforge.gg',
    '$2a$12$LQv3c1yqBWVHxkd0LHAkCOYz6TtxMQJqhN8/lewdBEPPv5aZkBFJO',
    'admin'
) ON CONFLICT (email) DO NOTHING;
