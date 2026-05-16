-- Create database and switch to it
CREATE DATABASE IF NOT EXISTS chatdb
  DEFAULT CHARACTER SET = utf8mb4
  DEFAULT COLLATE = utf8mb4_unicode_ci;
USE chatdb;

-- Users table
CREATE TABLE IF NOT EXISTS users (
  username BIGINT NOT NULL PRIMARY KEY,
  password VARCHAR(20) NOT NULL,
  display_name VARCHAR(40) NOT NULL UNIQUE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  last_login TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  status TINYINT NOT NULL DEFAULT 1
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Public messages
CREATE TABLE IF NOT EXISTS public_messages (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  from_username BIGINT NOT NULL,
  msg VARCHAR(512) NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  FOREIGN KEY (from_username) REFERENCES users(username) ON DELETE CASCADE,
  INDEX idx_pub_from_user (from_username),
  INDEX idx_pub_created_at (created_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Private messages
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

-- User presence
CREATE TABLE IF NOT EXISTS user_presence (
  user_id BIGINT NOT NULL PRIMARY KEY,
  last_seen TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  online TINYINT NOT NULL DEFAULT 1,
  FOREIGN KEY (user_id) REFERENCES users(username) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;