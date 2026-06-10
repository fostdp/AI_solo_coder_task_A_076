CREATE DATABASE IF NOT EXISTS cavitation_monitor;

USE cavitation_monitor;

CREATE TABLE IF NOT EXISTS raw_signal
(
    timestamp       DateTime64(3),
    turbine_id      UInt8,
    sensor_id       UInt8,
    sensor_type     Enum8('hydrophone' = 1, 'accelerometer' = 2),
    location        Enum8('spiral_case_inlet' = 1, 'after_guide_vane' = 2, 'draft_tube' = 3, 'runner_inlet' = 4, 'runner_outlet' = 5, 'blade_channel' = 6),
    sample_rate     UInt32,
    signal_data     Array(Float32),
    signal_length   UInt32
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, sensor_id, timestamp)
TTL timestamp + INTERVAL 7 DAY,
    timestamp + INTERVAL 3 DAY TO VOLUME 'cold'
SETTINGS index_granularity = 8192,
    storage_policy = 'tiered',
    move_factor = 0.2,
    storage_policy = 'tiered';

CREATE TABLE IF NOT EXISTS spectrum_feature
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    sensor_id           UInt8,
    fft_magnitude       Array(Float32),
    fft_length          UInt32,
    dominant_freq       Float32,
    total_power         Float64,
    band_power_low      Float64,
    band_power_mid      Float64,
    band_power_high     Float64,
    wavelet_energy      Array(Float32),
    wavelet_levels      UInt8
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, sensor_id, timestamp)
TTL timestamp + INTERVAL 90 DAY,
    timestamp + INTERVAL 30 DAY TO VOLUME 'cold'
SETTINGS index_granularity = 8192,
    move_factor = 0.2,
    storage_policy = 'tiered';

CREATE TABLE IF NOT EXISTS cavitation_status
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    blade_id            UInt8,
    zone_id             UInt8,
    cavitation_stage    Enum8('none' = 0, 'incipient' = 1, 'critical' = 2, 'developed' = 3),
    cavitation_intensity Float32,
    anomaly_score       Float32,
    detection_method    Enum8('autoencoder' = 1, 'isolation_forest' = 2, 'ensemble' = 3)
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, blade_id, timestamp)
TTL timestamp + INTERVAL 365 DAY,
    timestamp + INTERVAL 90 DAY TO VOLUME 'cold'
SETTINGS index_granularity = 8192,
    move_factor = 0.2,
    storage_policy = 'tiered';

CREATE TABLE IF NOT EXISTS fatigue_damage
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    blade_id            UInt8,
    zone_id             UInt8,
    stress_amplitude    Float32,
    stress_mean         Float32,
    cycle_count         UInt64,
    miner_damage        Float64,
    cumulative_damage   Float64,
    remaining_life_hours Float64,
    sn_params           String
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, blade_id, timestamp)
TTL timestamp + INTERVAL 365 DAY,
    timestamp + INTERVAL 90 DAY TO VOLUME 'cold'
SETTINGS index_granularity = 8192,
    move_factor = 0.2,
    storage_policy = 'tiered';

CREATE TABLE IF NOT EXISTS alarm_record
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    alarm_type          Enum8('cavitation_overlimit' = 1, 'vibration_overlimit' = 2, 'fatigue_warning' = 3, 'cavitation_developed' = 4),
    severity            Enum8('info' = 1, 'warning' = 2, 'critical' = 3),
    blade_id            UInt8,
    zone_id             UInt8,
    measured_value      Float64,
    threshold_value     Float64,
    iec61850_sent       UInt8,
    maintenance_advice  String
)
ENGINE = MergeTree()
PARTITION BY toYYYYMMDD(timestamp)
ORDER BY (turbine_id, timestamp)
TTL timestamp + INTERVAL 180 DAY,
    timestamp + INTERVAL 60 DAY TO VOLUME 'cold'
SETTINGS index_granularity = 8192,
    move_factor = 0.2,
    storage_policy = 'tiered';

CREATE TABLE IF NOT EXISTS cavitation_status_archive
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    blade_id            UInt8,
    zone_id             UInt8,
    cavitation_stage    Enum8('none' = 0, 'incipient' = 1, 'critical' = 2, 'developed' = 3),
    avg_intensity       Float32,
    max_intensity       Float32,
    anomaly_score_p95   Float32,
    sample_count        UInt64
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, blade_id, timestamp)
TTL timestamp + INTERVAL 3 YEAR
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS fatigue_damage_archive
(
    timestamp           DateTime64(3),
    turbine_id          UInt8,
    blade_id            UInt8,
    zone_id             UInt8,
    max_stress_amplitude Float32,
    total_cycle_count   UInt64,
    max_miner_damage    Float64,
    cumulative_damage   Float64,
    min_remaining_life  Float64
)
ENGINE = MergeTree()
PARTITION BY toYYYYMM(timestamp)
ORDER BY (turbine_id, blade_id, timestamp)
TTL timestamp + INTERVAL 5 YEAR
SETTINGS index_granularity = 8192;

CREATE MATERIALIZED VIEW IF NOT EXISTS cavitation_status_daily_mv
TO cavitation_status_archive
AS
SELECT
    toStartOfDay(timestamp) AS timestamp,
    turbine_id,
    blade_id,
    zone_id,
    cavitation_stage,
    avg(cavitation_intensity) AS avg_intensity,
    max(cavitation_intensity) AS max_intensity,
    quantile(0.95)(anomaly_score) AS anomaly_score_p95,
    count() AS sample_count
FROM cavitation_status
GROUP BY timestamp, turbine_id, blade_id, zone_id, cavitation_stage;

CREATE MATERIALIZED VIEW IF NOT EXISTS fatigue_damage_daily_mv
TO fatigue_damage_archive
AS
SELECT
    toStartOfDay(timestamp) AS timestamp,
    turbine_id,
    blade_id,
    zone_id,
    max(stress_amplitude) AS max_stress_amplitude,
    sum(cycle_count) AS total_cycle_count,
    max(miner_damage) AS max_miner_damage,
    max(cumulative_damage) AS cumulative_damage,
    min(remaining_life_hours) AS min_remaining_life
FROM fatigue_damage
GROUP BY timestamp, turbine_id, blade_id, zone_id;

CREATE TABLE IF NOT EXISTS turbine_config
(
    turbine_id          UInt8,
    turbine_name        String,
    rated_head          Float32,
    rated_flow          Float32,
    rated_speed         Float32,
    runner_diameter     Float32,
    blade_count         UInt8,
    sensor_count        UInt8,
    commissioning_date  Date,
    design_life_years   UInt16
)
ENGINE = ReplacingMergeTree()
ORDER BY turbine_id
SETTINGS index_granularity = 8192;

CREATE TABLE IF NOT EXISTS sensor_config
(
    sensor_id           UInt8,
    turbine_id          UInt8,
    sensor_type         Enum8('hydrophone' = 1, 'accelerometer' = 2),
    location            Enum8('spiral_case_inlet' = 1, 'after_guide_vane' = 2, 'draft_tube' = 3, 'runner_inlet' = 4, 'runner_outlet' = 5, 'blade_channel' = 6),
    blade_id            Nullable(UInt8),
    zone_id             Nullable(UInt8),
    sample_rate         UInt32,
    sensitivity         Float32,
    calibration_date    Date
)
ENGINE = ReplacingMergeTree()
ORDER BY (turbine_id, sensor_id)
SETTINGS index_granularity = 8192;

INSERT INTO turbine_config (turbine_id, turbine_name, rated_head, rated_flow, rated_speed, runner_diameter, blade_count, sensor_count, commissioning_date, design_life_years) VALUES
(1, '1号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2010-06-01', 40),
(2, '2号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2011-03-15', 40),
(3, '3号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2012-08-20', 40),
(4, '4号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2013-05-10', 40),
(5, '5号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2014-01-25', 40),
(6, '6号机组', 120.0, 300.0, 150.0, 5.0, 13, 20, '2015-11-08', 40);

INSERT INTO sensor_config (sensor_id, turbine_id, sensor_type, location, blade_id, zone_id, sample_rate, sensitivity, calibration_date) VALUES
(1,  1, 'hydrophone',     'spiral_case_inlet',   NULL, NULL, 1000, -180.0, '2025-01-10'),
(2,  1, 'hydrophone',     'after_guide_vane',    NULL, NULL, 1000, -180.0, '2025-01-10'),
(3,  1, 'hydrophone',     'draft_tube',          NULL, NULL, 1000, -180.0, '2025-01-10'),
(4,  1, 'hydrophone',     'runner_inlet',        1,    1,    1000, -180.0, '2025-01-10'),
(5,  1, 'hydrophone',     'runner_inlet',        1,    2,    1000, -180.0, '2025-01-10'),
(6,  1, 'hydrophone',     'runner_inlet',        1,    3,    1000, -180.0, '2025-01-10'),
(7,  1, 'hydrophone',     'blade_channel',       2,    1,    1000, -180.0, '2025-01-10'),
(8,  1, 'hydrophone',     'blade_channel',       2,    2,    1000, -180.0, '2025-01-10'),
(9,  1, 'hydrophone',     'blade_channel',       2,    3,    1000, -180.0, '2025-01-10'),
(10, 1, 'hydrophone',     'runner_outlet',       3,    1,    1000, -180.0, '2025-01-10'),
(11, 1, 'hydrophone',     'runner_outlet',       3,    2,    1000, -180.0, '2025-01-10'),
(12, 1, 'hydrophone',     'runner_outlet',       3,    3,    1000, -180.0, '2025-01-10'),
(13, 1, 'accelerometer',  'spiral_case_inlet',   NULL, NULL, 1000, 100.0,  '2025-01-10'),
(14, 1, 'accelerometer',  'after_guide_vane',    NULL, NULL, 1000, 100.0,  '2025-01-10'),
(15, 1, 'accelerometer',  'draft_tube',          NULL, NULL, 1000, 100.0,  '2025-01-10'),
(16, 1, 'accelerometer',  'runner_inlet',        1,    1,    1000, 100.0,  '2025-01-10'),
(17, 1, 'accelerometer',  'runner_inlet',        1,    2,    1000, 100.0,  '2025-01-10'),
(18, 1, 'accelerometer',  'blade_channel',       2,    1,    1000, 100.0,  '2025-01-10'),
(19, 1, 'accelerometer',  'blade_channel',       2,    2,    1000, 100.0,  '2025-01-10'),
(20, 1, 'accelerometer',  'draft_tube',          4,    1,    1000, 100.0,  '2025-01-10');
