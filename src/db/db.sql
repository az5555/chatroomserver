-- Create database and switch to it
CREATE DATABASE IF NOT EXISTS chatdb
  DEFAULT CHARACTER SET = utf8mb4
  DEFAULT COLLATE = utf8mb4_unicode_ci;
USE chatdb;

-- Users table: application MUST hash passwords (bcrypt/argon2) before storing
CREATE TABLE IF NOT EXISTS users (
  username BIGINT NOT NULL PRIMARY KEY,
  password VARCHAR(20) NOT NULL,
  display_name VARCHAR(40) NOT NULL UNIQUE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  status TINYINT NOT NULL DEFAULT 1, -- 1=active,0=disabled
  INDEX display_name (display_name)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Messages: to_user_id NULL => broadcast/public
-- Messages split into public_messages and private_messages

-- Public (broadcast) messages
CREATE TABLE IF NOT EXISTS public_messages (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  from_username BIGINT NOT NULL,
  msg VARCHAR(512) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (from_username) REFERENCES users(username) ON DELETE CASCADE,
  INDEX idx_pub_from_user (from_username),
  INDEX idx_pub_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Private (one-to-one) messages
CREATE TABLE IF NOT EXISTS private_messages (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  from_username BIGINT NOT NULL,
  to_username BIGINT NOT NULL,
  msg VARCHAR(512) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (from_username) REFERENCES users(username) ON DELETE CASCADE,
  FOREIGN KEY (to_username) REFERENCES users(username) ON DELETE CASCADE,
  INDEX idx_priv_to_user (to_username),
  INDEX idx_priv_from_user (from_username),
  INDEX idx_priv_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Migration: copy existing rows from `messages` into new tables then rename backup
-- Run migration during low traffic window; this assumes `messages` exists and has compatible columns.

-- keep backup before drop
RENAME TABLE messages TO messages_backup;

COMMIT;

-- After verifying migration, optionally remove the backup:
-- DROP TABLE IF EXISTS messages_backup;

-- Optional: simple persistent user presence table (update on login/logout)
CREATE TABLE IF NOT EXISTS user_presence (
  user_id BIGINT UNSIGNED PRIMARY KEY,
  last_seen TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  online TINYINT NOT NULL DEFAULT 1,
  FOREIGN KEY (user_id) REFERENCES users(username) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Example stored-proc (optional): create session token
DELIMITER $$
CREATE PROCEDURE sp_create_session(IN p_user_id BIGINT, IN p_token CHAR(64), IN p_ttl_seconds INT)
BEGIN
  INSERT INTO sessions(token, user_id, expires_at)
  VALUES (p_token, p_user_id, DATE_ADD(NOW(), INTERVAL p_ttl_seconds SECOND))
  ON DUPLICATE KEY UPDATE
    created_at = VALUES(created_at),
    expires_at = VALUES(expires_at);
END$$
DELIMITER ;

-- Safety: small helper to purge expired sessions (run from cron)
CREATE EVENT IF NOT EXISTS ev_purge_expired_sessions
ON SCHEDULE EVERY 1 HOUR
DO
  DELETE FROM sessions WHERE expires_at < NOW();

-- Purge messages older than 1 day (runs hourly)
-- Ensure MySQL event scheduler is enabled: `SET GLOBAL event_scheduler = ON;`
CREATE EVENT IF NOT EXISTS ev_purge_old_public_messages
ON SCHEDULE EVERY 1 HOUR
DO
  DELETE FROM public_messages WHERE created_at < NOW() - INTERVAL 1 DAY;

CREATE EVENT IF NOT EXISTS ev_purge_old_private_messages
ON SCHEDULE EVERY 1 HOUR
DO
  DELETE FROM private_messages WHERE created_at < NOW() - INTERVAL 1 DAY;

-- Notes:
-- - To enable permanently, set `event_scheduler=ON` in MySQL config (my.cnf) under [mysqld].
-- - Alternative: run the following from server or cron if events are not available:
--     DELETE FROM public_messages WHERE created_at < NOW() - INTERVAL 1 DAY;
--     DELETE FROM private_messages WHERE created_at < NOW() - INTERVAL 1 DAY;