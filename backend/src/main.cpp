#include "common.h"
#include "pxi_collector.h"
#include "cavitation_service.h"
#include "fatigue_service.h"
#include "alarm_service.h"
#include "api_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/model_config.json";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    ModelConfig config = ModelConfig::loadFromFile(config_path);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    signal(SIGBREAK, signalHandler);
#endif

    PXICollector collector(config.udp_port, config);
    CavitationService cavitation_svc(config);
    FatigueService fatigue_svc(config);
    AlarmService alarm_svc(config);
    ApiServer api_server(config.api_port);

    std::cout << "Starting modular cavitation monitor..." << std::endl;

    if (!collector.start()) {
        std::cerr << "Failed to start PXI collector" << std::endl;
        return 1;
    }

    if (!cavitation_svc.start()) {
        std::cerr << "Failed to start cavitation service" << std::endl;
        collector.stop();
        return 1;
    }

    if (!fatigue_svc.start()) {
        std::cerr << "Failed to start fatigue service" << std::endl;
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }

    if (!alarm_svc.start()) {
        std::cerr << "Failed to start alarm service" << std::endl;
        fatigue_svc.stop();
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }

    api_server.setTurbineListHandler([&alarm_svc]() -> std::vector<uint8_t> {
        return alarm_svc.getTurbineList();
    });

    api_server.setCavitationStatusHandler([&alarm_svc](uint8_t turbine_id) -> CavitationStatus {
        return alarm_svc.getLatestCavitation(turbine_id);
    });

    api_server.setCavitationHistoryHandler(
        [&alarm_svc](uint8_t turbine_id, uint8_t limit) -> std::vector<CavitationStatus> {
        std::vector<CavitationStatus> result;
        auto status = alarm_svc.getLatestCavitation(turbine_id);
        if (status.turbine_id == turbine_id) {
            result.push_back(status);
        }
        return result;
    });

    api_server.setFatigueInfoHandler(
        [&alarm_svc](uint8_t turbine_id, uint8_t blade_id) -> FatigueDamage {
        return alarm_svc.getLatestFatigue(turbine_id, blade_id);
    });

    api_server.setSpectrumHandler(
        [&collector](uint8_t turbine_id, uint8_t sensor_id) -> SpectrumFeature {
        SpectrumFeature feat{};
        feat.turbine_id = turbine_id;
        feat.sensor_id = sensor_id;
        return feat;
    });

    api_server.setWaterfallHandler(
        [&collector](uint8_t turbine_id, uint8_t sensor_id, int depth) -> std::vector<SpectrumFeature> {
        return {};
    });

    api_server.setAlarmListHandler(
        [&alarm_svc](AlarmSeverity severity) -> std::vector<AlarmRecord> {
        return alarm_svc.getRecentAlarms(severity);
    });

    api_server.setConfigHandler([&config]() -> std::string {
        nlohmann::json j;
        j["sample_rate"] = config.sample_rate;
        j["fft_size"] = config.fft_size;
        j["wavelet_levels"] = config.wavelet_levels;
        j["detection_method"] = "ensemble";
        j["if_weight"] = config.ensemble_if_weight;
        j["ae_weight"] = config.ensemble_ae_weight;
        j["cavitation_factor"] = config.cavitation_factor;
        j["design_life_hours"] = config.design_life_hours;
        return j.dump();
    });

    api_server.setRealtimeHandler(
        [&alarm_svc](uint8_t turbine_id, std::function<void(const CavitationStatus&)> sender) {
        auto status = alarm_svc.getLatestCavitation(turbine_id);
        if (status.turbine_id == turbine_id) {
            sender(status);
        }
    });

    if (!api_server.start()) {
        std::cerr << "Failed to start API server" << std::endl;
        alarm_svc.stop();
        fatigue_svc.stop();
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }

    std::cout << "Cavitation Monitor started (modular architecture):" << std::endl;
    std::cout << "  UDP port: " << config.udp_port << std::endl;
    std::cout << "  API port: " << config.api_port << std::endl;
    std::cout << "  Pipeline: collector -> cavitation -> fatigue -> alarm" << std::endl;

    std::vector<std::vector<float>> training_buffer;
    std::mutex training_mutex;
    auto last_training_time = std::chrono::steady_clock::now();

    while (g_running) {
        CollectorOutput collector_out;
        while (collector.output_queue.tryPop(collector_out)) {
            cavitation_svc.input_queue.tryPush(collector_out);

            {
                std::lock_guard<std::mutex> lock(training_mutex);
                std::vector<float> feat_vec;
                feat_vec.push_back(static_cast<float>(collector_out.feature.band_power_low));
                feat_vec.push_back(static_cast<float>(collector_out.feature.band_power_mid));
                feat_vec.push_back(static_cast<float>(collector_out.feature.band_power_high));
                for (float e : collector_out.feature.wavelet_energy) {
                    feat_vec.push_back(e);
                }
                feat_vec.resize(config.ae_input_dim, 0.0f);
                training_buffer.push_back(feat_vec);
                if (training_buffer.size() > 5000) {
                    training_buffer.erase(training_buffer.begin());
                }
            }
        }

        DetectorOutput detector_out;
        while (cavitation_svc.output_queue.tryPop(detector_out)) {
            fatigue_svc.input_queue.tryPush(detector_out);
        }

        EvaluatorOutput evaluator_out;
        while (fatigue_svc.output_queue.tryPop(evaluator_out)) {
            alarm_svc.input_queue.tryPush(evaluator_out);
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_training_time);

        if (elapsed.count() >= 5) {
            std::lock_guard<std::mutex> lock(training_mutex);
            if (training_buffer.size() >= 100) {
                std::cout << "Retraining models with " << training_buffer.size()
                          << " samples..." << std::endl;
                cavitation_svc.trainModel(training_buffer, 10);
                training_buffer.clear();
                std::cout << "Model retraining complete." << std::endl;
            }
            last_training_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "Shutting down..." << std::endl;

    alarm_svc.stop();
    fatigue_svc.stop();
    cavitation_svc.stop();
    collector.stop();
    api_server.stop();

    std::cout << "Shutdown complete." << std::endl;
    return 0;
}
