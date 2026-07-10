CREATE TABLE IF NOT EXISTS need_chat_summary (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,

    summary_type ENUM('hourly', 'daily') NOT NULL DEFAULT 'hourly',

    chat_type ENUM('O', 'W', 'P', 'G', 'M', 'C', 'ALL') NOT NULL DEFAULT 'O',
    chat_type_label VARCHAR(20) NOT NULL DEFAULT '일반',

    period_start DATETIME NOT NULL,
    period_end DATETIME NOT NULL,

    source_count INT NOT NULL DEFAULT 0,

    summary MEDIUMTEXT NOT NULL,
    issues MEDIUMTEXT NULL,
    keywords TEXT NULL,

    model VARCHAR(64) NULL,

    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    UNIQUE KEY uq_need_chat_summary_period (
        summary_type,
        chat_type,
        period_start,
        period_end
    )
);