-- ============================================================================
-- Cangjie IME Database Schema
-- SQLite version
-- Supports:
--   - ime_character: per-character popularity
--   - ime_cangjie_mapping: Cangjie code to character mappings
--   - ime_character_associated_word: N-gram associated words
--
-- Notes:
--   - SQLite uses INTEGER PRIMARY KEY for auto-generated row ids.
--   - Runtime should open this database read-only.
--   - Foreign key enforcement must also be enabled per SQLite connection with
--     PRAGMA foreign_keys = ON.
-- ============================================================================

PRAGMA foreign_keys = ON;

-- --------------------------------------------------------------------------
-- Drop existing tables (if any)
-- NOTE: Order matters due to foreign key dependencies
-- --------------------------------------------------------------------------
DROP TABLE IF EXISTS ime_character_associated_word;
DROP TABLE IF EXISTS ime_cangjie_mapping;
DROP TABLE IF EXISTS ime_character;
DROP TABLE IF EXISTS ime_metadata;

-- ============================================================================
-- 1. ime_character
--    One row per Unicode character, with offline-built popularity.
-- ============================================================================

CREATE TABLE ime_character (
    id           INTEGER PRIMARY KEY,
    character    TEXT NOT NULL,
    usage_count  INTEGER NOT NULL DEFAULT 0,

    CONSTRAINT uq_ime_character_character
        UNIQUE (character),

    CONSTRAINT ck_ime_character_usage_count_nonneg
        CHECK (usage_count >= 0)
);

-- Character lookup by character value (supports joins and quick lookups)
CREATE INDEX ix_ime_character_character
    ON ime_character (character);

-- Optional: index by popularity if you ever need top characters
CREATE INDEX ix_ime_character_usage
    ON ime_character (usage_count DESC, character);

-- ============================================================================
-- 2. ime_cangjie_mapping
--    Maps Cangjie codes to characters, preserving source file order.
--    One character can have multiple codes.
-- ============================================================================

CREATE TABLE ime_cangjie_mapping (
    id           INTEGER PRIMARY KEY,

    character    TEXT NOT NULL,
    code         TEXT NOT NULL,
    line_number  INTEGER NOT NULL,

    CONSTRAINT fk_ime_cangjie_character
        FOREIGN KEY (character)
        REFERENCES ime_character (character)
        ON UPDATE RESTRICT
        ON DELETE RESTRICT,

    -- A character + code pair must be unique.
    CONSTRAINT uq_ime_cangjie_character_code
        UNIQUE (character, code)
);

-- Index for runtime lookup by code (main search path)
CREATE INDEX ix_ime_cangjie_code
    ON ime_cangjie_mapping (code, character);

-- Index to support deterministic tie-breaking by line_number
CREATE INDEX ix_ime_cangjie_character_line
    ON ime_cangjie_mapping (character, line_number);

-- ============================================================================
-- 3. ime_character_associated_word
--    Stores associated phrases for a given leading_text context.
--
--    leading_text can be 1..LeadingMaxLength characters.
--    associated_text can be 1..AssociatedMaxLength characters.
--    The exact max lengths are controlled by rebuild config and enforced in
--    rebuild logic, not at schema level.
-- ============================================================================

CREATE TABLE ime_character_associated_word (
    id               INTEGER PRIMARY KEY,

    -- Leading context string, e.g. "耶", "耶和", "耶和華".
    leading_text     TEXT NOT NULL,

    -- Associated phrase, e.g. "和", "和華", "和華是".
    associated_text  TEXT NOT NULL,

    -- Offline-built popularity count.
    usage_count      INTEGER NOT NULL DEFAULT 0,

    CONSTRAINT uq_ime_assoc_leading_associated
        UNIQUE (leading_text, associated_text),

    CONSTRAINT ck_ime_assoc_usage_count_nonneg
        CHECK (usage_count >= 0)
);

-- Runtime lookup index:
--   WHERE leading_text = :context
--   ORDER BY usage_count DESC, associated_text ASC
CREATE INDEX ix_ime_assoc_leading_rank
    ON ime_character_associated_word (
        leading_text,
        usage_count DESC,
        associated_text ASC,
        id ASC
    );

-- ============================================================================
-- 4. Metadata
-- ============================================================================

CREATE TABLE ime_metadata (
    key    TEXT PRIMARY KEY,
    value  TEXT NOT NULL
);
