#include "udp_receiver.h"
#include "signal_processor.h"
#include "cavitation_detector.h"
#include "fatigue_analyzer.h"
#include "alarm_manager.h"
#include "clickhouse_writer.h"
#include "api_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

struct LatestState {
    std::unordered_map<uint8_t, CavitationStatus> cavitation_status;
    std::unordered_map<uint64_t, FatigueDamage> fatigue_damage;
    std::unordered_map<uint64_t, SpectrumFeature> spectrum_features;
    std::vector<AlarmRecord> recent_alarms;
    std::mutex mutex;
};

static uint64_t makeFatigueKey(uint8_t turbine_id, uint8_t blade_id) {
    return (static_cast<uint64_t>(turbine_id) << 8) | blade_id;
}

static uint64_t makeSpectrumKey(uint8_t turbine_id, uint8_t sensor_id) {
    return (static_cast<uint64_t>(turbine_id) << 8) | sensor_id;
}

int main(int argc, char* argv[]) {
    uint16_t udp_port = 9000;
    uint16_t api_port = 8080;
    std::string clickhouse_host = "host=localhost;port=9000;user=default;database=cavitation_monitor";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--udp-port" && i + 1 < argc) {
            udp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--api-port" && i + 1 < argc) {
            api_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--clickhouse-host" && i + 1 < argc) {
            clickhouse_host = argv[++i];
        }
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

#ifdef _WIN32
    signal(SIGBREAK, signalHandler);
#endif

    SignalProcessor signal_processor;
    signal_processor.setSampleRate(50000.0f);

    CavitationDetector cavitation_detector(19);
    FatigueAnalyzer fatigue_analyzer;
    AlarmManager alarm_manager;
    ClickHouseWriter ch_writer(clickhouse_host);
    ApiServer api_server(api_port);

    auto latest_state = std::make_shared<LatestState>();

    std::vector<std::vector<float>> training_buffer;
    std::mutex training_mutex;
    std::chrono::steady_clock::time_point last_training_time = std::chrono::steady_clock::now();

    UdpReceiver udp_receiver(udp_port);
    udp_receiver.setCallback([&](const SensorData& data) {
        SpectrumFeature feature = signal_processor.computeSpectralFeatures(data);

        {
            std::lock_guard<std::mutex> lock(latest_state->mutex);
            latest_state->spectrum_features[makeSpectrumKey(data.turbine_id, data.sensor_id)] = feature;
        }

        ch_writer.writeRawSignal(data);
        ch_writer.writeSpectrumFeature(feature);

        CavitationStatus cav_status = cavitation_detector.detect(feature);
        cav_status.turbine_id = data.turbine_id;

        {
            std::lock_guard<std::mutex> lock(latest_state->mutex);
            latest_state->cavitation_status[data.turbine_id] = cav_status;
        }

        ch_writer.writeCavitationStatus(cav_status);

        std::vector<float> stress_history;
        if (data.sensor_type == SensorType::ACCELEROMETER) {
            stress_history = data.samples;
        }
        if (!stress_history.empty()) {
            FatigueDamage fatigue = fatigue_analyzer.analyze(
                data.turbine_id, data.blade_id, data.zone_id,
                stress_history, cav_status.intensity);

            {
                std::lock_guard<std::mutex> lock(latest_state->mutex);
                latest_state->fatigue_damage[makeFatigueKey(data.turbine_id, data.blade_id)] = fatigue;
            }

            ch_writer.writeFatigueDamage(fatigue);
        }

        float vibration_velocity = 0.0f;
        if (data.sensor_type == SensorType::ACCELEROMETER && !data.samples.empty()) {
            double sum_sq = 0.0;
            for (float s : data.samples) {
                sum_sq += static_cast<double>(s) * static_cast<double>(s);
            }
            vibration_velocity = static_cast<float>(std::sqrt(sum_sq / data.samples.size()));
        }

        auto alarms = alarm_manager.checkAlarms(cav_status,
            latest_state->fatigue_damage[makeFatigueKey(data.turbine_id, data.blade_id)],
            vibration_velocity);

        {
            std::lock_guard<std::mutex> lock(latest_state->mutex);
            for (auto& alarm : alarms) {
                ch_writer.writeAlarmRecord(alarm);
                latest_state->recent_alarms.push_back(std::move(alarm));
            }
            if (latest_state->recent_alarms.size() > 1000) {
                latest_state->recent_alarms.erase(
                    latest_state->recent_alarms.begin(),
                    latest_state->recent_alarms.begin() +
                    (latest_state->recent_alarms.size() - 500));
            }
        }

        {
            std::lock_guard<std::mutex> lock(training_mutex);
            std::vector<float> feat_vec;
            feat_vec.push_back(static_cast<float>(feature.band_power_low));
            feat_vec.push_back(static_cast<float>(feature.band_power_mid));
            feat_vec.push_back(static_cast<float>(feature.band_power_high));
            for (float e : feature.wavelet_energy) {
                feat_vec.push_back(e);
            }
            feat_vec.resize(19, 0.0f);
            training_buffer.push_back(feat_vec);
            if (training_buffer.size() > 5000) {
                training_buffer.erase(training_buffer.begin());
            }
        }
    });

    api_server.setTurbineListHandler([latest_state]() -> std::vector<uint8_t> {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        std::vector<uint8_t> ids;
        for (const auto& [id, _] : latest_state->cavitation_status) {
            ids.push_back(id);
        }
        if (ids.empty()) {
            for (uint8_t i = 1; i <= 6; i++) ids.push_back(i);
        }
        return ids;
    });

    api_server.setCavitationStatusHandler([latest_state](uint8_t turbine_id) -> CavitationStatus {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        auto it = latest_state->cavitation_status.find(turbine_id);
        if (it != latest_state->cavitation_status.end()) {
            return it->second;
        }
        CavitationStatus status{};
        status.turbine_id = turbine_id;
        status.stage = CavitationStage::NONE;
        return status;
    });

    api_server.setCavitationHistoryHandler(
        [latest_state](uint8_t turbine_id, uint8_t limit) -> std::vector<CavitationStatus> {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        std::vector<CavitationStatus> result;
        auto it = latest_state->cavitation_status.find(turbine_id);
        if (it != latest_state->cavitation_status.end()) {
            result.push_back(it->second);
        }
        return result;
    });

    api_server.setFatigueInfoHandler(
        [latest_state](uint8_t turbine_id, uint8_t blade_id) -> FatigueDamage {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        auto it = latest_state->fatigue_damage.find(makeFatigueKey(turbine_id, blade_id));
        if (it != latest_state->fatigue_damage.end()) {
            return it->second;
        }
        FatigueDamage dmg{};
        dmg.turbine_id = turbine_id;
        dmg.blade_id = blade_id;
        dmg.remaining_life_hours = 100000.0;
        return dmg;
    });

    api_server.setSpectrumHandler(
        [latest_state](uint8_t turbine_id, uint8_t sensor_id) -> SpectrumFeature {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        auto it = latest_state->spectrum_features.find(makeSpectrumKey(turbine_id, sensor_id));
        if (it != latest_state->spectrum_features.end()) {
            return it->second;
        }
        SpectrumFeature feat{};
        feat.turbine_id = turbine_id;
        feat.sensor_id = sensor_id;
        return feat;
    });

    api_server.setWaterfallHandler(
        [latest_state](uint8_t turbine_id, uint8_t sensor_id, int depth) -> std::vector<SpectrumFeature> {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        std::vector<SpectrumFeature> result;
        auto it = latest_state->spectrum_features.find(makeSpectrumKey(turbine_id, sensor_id));
        if (it != latest_state->spectrum_features.end()) {
            result.push_back(it->second);
        }
        return result;
    });

    api_server.setAlarmListHandler(
        [latest_state](AlarmSeverity severity) -> std::vector<AlarmRecord> {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        std::vector<AlarmRecord> filtered;
        for (const auto& alarm : latest_state->recent_alarms) {
            if (static_cast<int>(alarm.severity) >= static_cast<int>(severity)) {
                filtered.push_back(alarm);
            }
        }
        return filtered;
    });

    api_server.setConfigHandler([]() -> std::string {
        nlohmann::json j;
        j["sample_rate"] = 50000;
        j["fft_size"] = 512;
        j["wavelet_levels"] = 4;
        j["detection_method"] = "ensemble";
        j["if_weight"] = 0.6;
        j["ae_weight"] = 0.4;
        return j.dump();
    });

    api_server.setRealtimeHandler(
        [latest_state](uint8_t turbine_id, std::function<void(const CavitationStatus&)> sender) {
        std::lock_guard<std::mutex> lock(latest_state->mutex);
        auto it = latest_state->cavitation_status.find(turbine_id);
        if (it != latest_state->cavitation_status.end()) {
            sender(it->second);
        }
    });

    if (!ch_writer.start()) {
        std::cerr << "Warning: ClickHouse writer failed to start, continuing without DB" << std::endl;
    }

    if (!api_server.start()) {
        std::cerr << "Failed to start API server" << std::endl;
        ch_writer.stop();
        return 1;
    }

    if (!udp_receiver.start()) {
        std::cerr << "Failed to start UDP receiver" << std::endl;
        api_server.stop();
        ch_writer.stop();
        return 1;
    }

    std::cout << "Cavitation Monitor started: UDP port=" << udp_port
              << " API port=" << api_port << std::endl;

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_training_time);

        if (elapsed.count() >= 5) {
            std::lock_guard<std::mutex> lock(training_mutex);
            if (training_buffer.size() >= 100) {
                std::cout << "Retraining models with " << training_buffer.size()
                          << " samples..." << std::endl;
                cavitation_detector.train(training_buffer, 10);
                training_buffer.clear();
                std::cout << "Model retraining complete." << std::endl;
            }
            last_training_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "Shutting down..." << std::endl;

    udp_receiver.stop();
    api_server.stop();
    ch_writer.stop();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
