#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SensorType : uint8_t {
    HYDROPHONE = 1,
    ACCELEROMETER = 2
};

enum class SensorLocation : uint8_t {
    SPIRAL_CASE_INLET = 1,
    AFTER_GUIDE_VANE = 2,
    DRAFT_TUBE = 3,
    RUNNER_INLET = 4,
    RUNNER_OUTLET = 5,
    BLADE_CHANNEL = 6
};

enum class CavitationStage : uint8_t {
    NONE = 0,
    INCIPIENT = 1,
    CRITICAL = 2,
    DEVELOPED = 3
};

enum class AlarmSeverity : uint8_t {
    INFO = 1,
    WARNING = 2,
    CRITICAL = 3
};

enum class AlarmType : uint8_t {
    CAVITATION_OVERLIMIT = 1,
    VIBRATION_OVERLIMIT = 2,
    FATIGUE_WARNING = 3,
    CAVITATION_DEVELOPED = 4
};

enum class DetectionMethod : uint8_t {
    AUTOENCODER = 1,
    ISOLATION_FOREST = 2,
    ENSEMBLE = 3
};

struct PXIPacketHeader {
    uint16_t magic;
    uint8_t version;
    uint8_t turbine_id;
    uint32_t seq;
    uint64_t timestamp_ms;
    uint8_t sensor_type;
    uint8_t sensor_id;
    uint16_t sample_count;
};

struct SensorData {
    uint8_t turbine_id;
    uint8_t sensor_id;
    SensorType sensor_type;
    SensorLocation location;
    uint8_t blade_id;
    uint8_t zone_id;
    uint64_t timestamp_ms;
    std::vector<float> samples;
};

struct SpectrumFeature {
    uint8_t turbine_id;
    uint8_t sensor_id;
    uint64_t timestamp_ms;
    std::vector<float> fft_magnitude;
    float dominant_freq;
    double total_power;
    double band_power_low;
    double band_power_mid;
    double band_power_high;
    std::vector<float> wavelet_energy;
};

struct CavitationStatus {
    uint8_t turbine_id;
    uint8_t blade_id;
    uint8_t zone_id;
    uint64_t timestamp_ms;
    CavitationStage stage;
    float intensity;
    float anomaly_score;
    DetectionMethod detection_method;
};

struct FatigueDamage {
    uint8_t turbine_id;
    uint8_t blade_id;
    uint8_t zone_id;
    uint64_t timestamp_ms;
    float stress_amplitude;
    float stress_mean;
    uint64_t cycle_count;
    double miner_damage;
    double cumulative_damage;
    double remaining_life_hours;
};

struct AlarmRecord {
    uint8_t turbine_id;
    AlarmType alarm_type;
    AlarmSeverity severity;
    uint8_t blade_id;
    uint8_t zone_id;
    uint64_t timestamp_ms;
    double measured_value;
    double threshold_value;
    bool iec61850_sent;
    std::string maintenance_advice;
};

constexpr uint16_t PXI_MAGIC = 0xCA5C;
constexpr uint8_t PXI_VERSION = 1;
constexpr uint16_t MAX_PAYLOAD_SAMPLES = 256;
constexpr size_t UDP_BUFFER_SIZE = 4096;
