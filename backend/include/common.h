#pragma once

#include <array>
#include <atomic>
#include <cstddef>
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

struct OperatingCondition {
    float head;
    float flow;
    float rpm;
    float power;
};

constexpr uint16_t PXI_MAGIC = 0xCA5C;
constexpr uint8_t PXI_VERSION = 1;
constexpr uint16_t MAX_PAYLOAD_SAMPLES = 256;
constexpr size_t UDP_BUFFER_SIZE = 4096;

struct CollectorOutput {
    SensorData raw_data;
    SpectrumFeature feature;
};

struct DetectorOutput {
    CavitationStatus cavitation;
    SpectrumFeature feature;
    SensorData raw_data;
};

struct EvaluatorOutput {
    CavitationStatus cavitation;
    FatigueDamage fatigue;
    float vibration_velocity;
};

template <typename T, size_t Capacity = 8192>
class MPSCQueue {
public:
    MPSCQueue() : head_(0), tail_(0) {
        for (size_t i = 0; i < Capacity; i++) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue& operator=(const MPSCQueue&) = delete;

    bool tryPush(const T& item) {
        size_t pos = head_.load(std::memory_order_relaxed);
        for (;;) {
            Cell& cell = cells_[pos & MASK];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (head_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = item;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }
    }

    bool tryPop(T& item) {
        size_t pos = tail_.load(std::memory_order_relaxed);
        Cell& cell = cells_[pos & MASK];
        size_t seq = cell.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
        if (diff == 0) {
            tail_.store(pos + 1, std::memory_order_relaxed);
            item = cell.data;
            cell.sequence.store(pos + MASK + 1, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool empty() const {
        return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
    }

    size_t size() const {
        return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed);
    }

private:
    static constexpr size_t MASK = Capacity - 1;

    struct Cell {
        std::atomic<size_t> sequence{0};
        T data{};
    };

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    std::array<Cell, Capacity> cells_;
};

struct ModelConfig {
    int if_n_trees = 100;
    int if_sample_size = 256;
    int ae_input_dim = 19;
    int ae_encoding_dim = 16;
    float ae_learning_rate = 0.001f;
    int ae_train_epochs = 50;
    int ae_warmstart_epochs = 5;
    float ae_warmstart_lr_factor = 0.1f;
    float ensemble_if_weight = 0.6f;
    float ensemble_ae_weight = 0.4f;

    float adaptive_base_incipient = 0.3f;
    float adaptive_base_critical = 0.5f;
    float adaptive_base_developed = 0.7f;
    size_t adaptive_window_size = 512;
    float adaptive_max_offset = 0.15f;

    float cond_shift_tolerance = 0.15f;
    size_t warmstart_pool_size = 256;
    size_t normalizer_history_size = 512;
    float normalizer_head_tolerance = 5.0f;
    float normalizer_power_tolerance = 20.0f;

    float cavitation_factor = 1.5f;
    double design_life_hours = 100000.0;

    struct SNSpectrum {
        int num_points = 8;
        double N[8] = {1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
        double S[8] = {620.0, 480.0, 380.0, 300.0, 240.0, 200.0, 180.0, 170.0};
    } sn_curve;

    float sample_rate = 50000.0f;
    int fft_size = 512;
    int wavelet_levels = 4;

    float alarm_cavitation_intensity_limit = 0.6f;
    float alarm_cavitation_developed_limit = 0.8f;
    float alarm_vibration_velocity_limit = 5.0f;
    float alarm_fatigue_damage_limit = 0.7f;

    uint16_t udp_port = 9000;
    uint16_t api_port = 8080;
    std::string clickhouse_connection = "host=localhost;port=9000;user=default;database=cavitation_monitor";

    static ModelConfig loadFromFile(const std::string& path);
};
