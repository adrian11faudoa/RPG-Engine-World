-- migrations/003_constraints.sql
-- Data integrity constraints, check constraints, and foreign key
-- improvements not included in the initial schema.

-- ─── campaigns ───────────────────────────────────────────────
-- Title must be non-empty
ALTER TABLE campaigns
    ADD CONSTRAINT chk_campaign_title_nonempty
    CHECK (length(trim(title)) > 0);

-- world_state must be valid JSON object (not array or scalar)
ALTER TABLE campaigns
    ADD CONSTRAINT chk_world_state_is_object
    CHECK (jsonb_typeof(world_state) = 'object');

-- ─── journal_entries ─────────────────────────────────────────
ALTER TABLE journal_entries
    ADD CONSTRAINT chk_journal_title_nonempty
    CHECK (length(trim(title)) > 0);

-- ─── game_sessions ───────────────────────────────────────────
-- ended_at must be after created_at when set
ALTER TABLE game_sessions
    ADD CONSTRAINT chk_session_end_after_start
    CHECK (ended_at IS NULL OR ended_at >= created_at);

-- player_count non-negative
ALTER TABLE game_sessions
    ADD CONSTRAINT chk_session_player_count_nonneg
    CHECK (player_count >= 0);

-- ─── roll_log ────────────────────────────────────────────────
-- formula must be non-empty
ALTER TABLE roll_log
    ADD CONSTRAINT chk_roll_formula_nonempty
    CHECK (length(trim(formula)) > 0);

-- result must be a JSON object with a 'total' field
ALTER TABLE roll_log
    ADD CONSTRAINT chk_roll_result_has_total
    CHECK (result ? 'total');

-- ─── assets ──────────────────────────────────────────────────
-- S3 key must be non-empty and not start with /
ALTER TABLE assets
    ADD CONSTRAINT chk_asset_key_format
    CHECK (length(key) > 0 AND key NOT LIKE '/%');

-- size_bytes non-negative
ALTER TABLE assets
    ADD CONSTRAINT chk_asset_size_nonneg
    CHECK (size_bytes IS NULL OR size_bytes >= 0);

-- folder must be a known value
ALTER TABLE assets
    ADD CONSTRAINT chk_asset_folder_valid
    CHECK (folder IN ('maps','miniatures','audio','mods','portraits','misc'));

-- ─── users ───────────────────────────────────────────────────
-- Username: 3-30 chars, alphanumeric + underscore + hyphen only
ALTER TABLE users
    ADD CONSTRAINT chk_username_format
    CHECK (username ~ '^[a-zA-Z0-9_-]{3,30}$');

-- Email: basic format check (real validation done in app layer)
ALTER TABLE users
    ADD CONSTRAINT chk_email_format
    CHECK (email ~ '^[^@\s]+@[^@\s]+\.[^@\s]+$');

-- ─── npcs ────────────────────────────────────────────────────
ALTER TABLE npcs
    ADD CONSTRAINT chk_npc_name_nonempty
    CHECK (length(trim(name)) > 0);

ALTER TABLE npcs
    ADD CONSTRAINT chk_npc_notes_is_object
    CHECK (jsonb_typeof(notes) = 'object');

-- ─── quests ──────────────────────────────────────────────────
ALTER TABLE quests
    ADD CONSTRAINT chk_quest_title_nonempty
    CHECK (length(trim(title)) > 0);

ALTER TABLE quests
    ADD CONSTRAINT chk_quest_objectives_is_array
    CHECK (jsonb_typeof(objectives) = 'array');

-- ─── campaign_players ────────────────────────────────────────
-- A user can't be both GM and a player in the same campaign
-- (enforced via: if gm_id = user_id, don't insert into campaign_players)
ALTER TABLE campaign_players
    ADD CONSTRAINT chk_player_not_gm
    CHECK (
        NOT EXISTS (
            SELECT 1 FROM campaigns c
            WHERE c.id = campaign_id AND c.gm_id = user_id
        )
    );

-- ─── Verify constraints installed ────────────────────────────
DO $$
BEGIN
    RAISE NOTICE '003_constraints.sql applied successfully — % constraints active',
        (SELECT COUNT(*) FROM pg_constraint WHERE contype = 'c'
         AND conrelid IN ('campaigns'::regclass, 'users'::regclass,
                          'assets'::regclass, 'roll_log'::regclass,
                          'game_sessions'::regclass));
END $$;
