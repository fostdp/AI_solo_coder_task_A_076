#include "common.h"
#include "pxi_collector.h"
#include "cavitation_service.h"
#include "fatigue_service.h"
#include "alarm_service.h"
#include "api_server.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>

#include <prometheus/registry.h>
#include <prometheus/exposer.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>

static std::atomic<bool> g_running{true};

static void signalHandler(int) {
    g_running = false;
}

struct Metrics {
    prometheus::Counter& udp_packets_received;
    prometheus::Counter& udp_bytes_received;
    prometheus::Counter& fft_computed;
    prometheus::Counter& cavitation_detected;
    prometheus::Counter& alarms_fired;
    prometheus::Counter& iec61850_sent;
    prometheus::Gauge& queue_collector_depth;
    prometheus::Gauge& queue_cavitation_depth;
    prometheus::Gauge& queue_fatigue_depth;
    prometheus::Gauge& queue_alarm_depth;
    prometheus::Histogram& pipeline_latency_ms;
    prometheus::Gauge& active_cavitation_blades;
    prometheus::Counter& model_retrain_total;

    static Metrics create(std::shared_ptr<prometheus::Registry> registry) {
        auto& udp_family = prometheus::BuildCounter()
            .Name("cavitation_udp_packets_total")
            .Help("Total UDP packets received")
            .Register(*registry);
        auto& bytes_family = prometheus::BuildCounter()
            .Name("cavitation_udp_bytes_total")
            .Help("Total UDP bytes received")
            .Register(*registry);
        auto& fft_family = prometheus::BuildCounter()
            .Name("cavitation_fft_computed_total")
            .Help("Total FFT computations")
            .Register(*registry);
        auto& cav_family = prometheus::BuildCounter()
            .Name("cavitation_detected_total")
            .Help("Total cavitation detections by stage")
            .Register(*registry);
        auto& alarm_family = prometheus::BuildCounter()
            .Name("cavitation_alarms_fired_total")
            .Help("Total alarms fired by severity")
            .Register(*registry);
        auto& iec_family = prometheus::BuildCounter()
            .Name("cavitation_iec61850_sent_total")
            .Help("Total IEC 61850 reports sent")
            .Register(*registry);
        auto& queue_family = prometheus::BuildGauge()
            .Name("cavitation_queue_depth")
            .Help("Current MPSC queue depth")
            .Register(*registry);
        auto& latency_family = prometheus::BuildHistogram()
            .Name("cavitation_pipeline_latency_ms")
            .Help("Pipeline end-to-end latency")
            .Register(*registry);
        auto& active_family = prometheus::BuildGauge()
            .Name("cavitation_active_blades")
            .Help("Number of blades with active cavitation")
            .Register(*registry);
        auto& retrain_family = prometheus::BuildCounter()
            .Name("cavitation_model_retrain_total")
            .Help("Total model retraining cycles")
            .Register(*registry);

        return Metrics{
            udp_family.Add({}),
            bytes_family.Add({}),
            fft_family.Add({}),
            cav_family.Add({}),
            alarm_family.Add({}),
            iec_family.Add({}),
            queue_family.Add({{"queue", "collector"}}),
            queue_family.Add({{"queue", "cavitation"}}),
            queue_family.Add({{"queue", "fatigue"}}),
            queue_family.Add({{"queue", "alarm"}}),
            latency_family.Add({}, prometheus::Histogram::BucketBoundaries{0.1, 0.5, 1, 2, 5, 10, 20, 50, 100}),
            active_family.Add({}),
            retrain_family.Add({})
        };
    }
};

int main(int argc, char* argv[]) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "logs/cavitation_monitor.log", 10 * 1024 * 1024, 5);
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    auto logger = std::make_shared<spdlog::async_logger>(
        "main", sinks.begin(), sinks.end(), spdlog::thread_pool::create(8192, 1));
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);

    spdlog::info("Cavitation Monitor v2.0.0 starting...");

    std::string config_path = "config/model_config.json";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    ModelConfig config;
    try {
        config = ModelConfig::loadFromFile(config_path);
        spdlog::info("Loaded config from {}", config_path);
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load config {}: {}, using defaults", config_path, e.what());
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#ifdef _WIN32
    signal(SIGBREAK, signalHandler);
#endif

    auto prom_registry = std::make_shared<prometheus::Registry>();
    prometheus::Exposer prom_exposer("0.0.0.0:9090");
    prom_exposer.RegisterCollectable(prom_registry);

    auto metrics = Metrics::create(prom_registry);

    spdlog::info("Prometheus metrics exposed on :9090/metrics");

    PXICollector collector(config.udp_port, config);
    CavitationService cavitation_svc(config);
    FatigueService fatigue_svc(config);
    AlarmService alarm_svc(config);
    ApiServer api_server(config.api_port);

    if (!collector.start()) {
        spdlog::critical("Failed to start PXI collector");
        return 1;
    }
    spdlog::info("PXI collector started on UDP port {}", config.udp_port);

    if (!cavitation_svc.start()) {
        spdlog::critical("Failed to start cavitation service");
        collector.stop();
        return 1;
    }
    spdlog::info("Cavitation detector service started");

    if (!fatigue_svc.start()) {
        spdlog::critical("Failed to start fatigue service");
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }
    spdlog::info("Fatigue evaluator service started");

    if (!alarm_svc.start()) {
        spdlog::critical("Failed to start alarm service");
        fatigue_svc.stop();
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }
    spdlog::info("Alarm pusher service started");

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
        spdlog::critical("Failed to start API server");
        alarm_svc.stop();
        fatigue_svc.stop();
        cavitation_svc.stop();
        collector.stop();
        return 1;
    }
    spdlog::info("API server started on port {}", config.api_port);

    spdlog::info("Pipeline: collector -> cavitation -> fatigue -> alarm");
    spdlog::info("System ready. Press Ctrl+C to shutdown.");

    std::vector<std::vector<float>> training_buffer;
    std::mutex training_mutex;
    auto last_training_time = std::chrono::steady_clock::now();
    auto last_metrics_time = std::chrono::steady_clock::now();

    while (g_running) {
        CollectorOutput collector_out;
        size_t collector_count = 0;
        while (collector.output_queue.tryPop(collector_out)) {
            auto pipeline_start = std::chrono::steady_clock::now();

            cavitation_svc.input_queue.tryPush(collector_out);
            metrics.udp_packets_received.Increment();
            metrics.fft_computed.Increment();
            collector_count++;

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

            if (detector_out.cavitation.stage != CavitationStage::NONE) {
                metrics.cavitation_detected.Increment();
            }
        }

        EvaluatorOutput evaluator_out;
        while (fatigue_svc.output_queue.tryPop(evaluator_out)) {
            alarm_svc.input_queue.tryPush(evaluator_out);

            metrics.pipeline_latency_ms.Observe(
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - std::chrono::steady_clock::time_point{}
                ).count() * 0.001
            );
        }

        auto now = std::chrono::steady_clock::now();

        auto metrics_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_metrics_time);
        if (metrics_elapsed.count() >= 2) {
            metrics.queue_collector_depth.Set(
                static_cast<double>(collector.output_queue.size()));
            metrics.queue_cavitation_depth.Set(
                static_cast<double>(cavitation_svc.input_queue.size()));
            metrics.queue_fatigue_depth.Set(
                static_cast<double>(fatigue_svc.input_queue.size()));
            metrics.queue_alarm_depth.Set(
                static_cast<double>(alarm_svc.input_queue.size()));
            last_metrics_time = now;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - last_training_time);
        if (elapsed.count() >= 5) {
            std::lock_guard<std::mutex> lock(training_mutex);
            if (training_buffer.size() >= 100) {
                spdlog::info("Retraining models with {} samples...", training_buffer.size());
                cavitation_svc.trainModel(training_buffer, 10);
                training_buffer.clear();
                metrics.model_retrain_total.Increment();
                spdlog::info("Model retraining complete");
            }
            last_training_time = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    spdlog::info("Shutting down...");

    alarm_svc.stop();
    fatigue_svc.stop();
    cavitation_svc.stop();
    collector.stop();
    api_server.stop();

    spdlog::info("Shutdown complete.");
    return 0;
}
