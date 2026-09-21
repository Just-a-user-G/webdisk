USE webdisk;

CREATE TABLE IF NOT EXISTS `tbl_upload_session` (
  `id` varchar(64) NOT NULL COMMENT '上传会话ID',
  `uid` int NOT NULL,
  `filename` varchar(255) NOT NULL,
  `total_chunks` int NOT NULL,
  `received_chunks` int DEFAULT 0,
  `total_size` bigint DEFAULT 0,
  `status` int DEFAULT 0 COMMENT '0=进行中, 1=已完成, 2=失败',
  `created_at` datetime DEFAULT CURRENT_TIMESTAMP,
  `last_update` datetime DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  KEY `idx_uid` (`uid`),
  KEY `idx_status` (`status`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
