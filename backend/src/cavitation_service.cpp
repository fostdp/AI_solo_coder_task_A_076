#include "cavitation_service.h"

#include <chrono>

#include <spdlog/spdlog.h>

CavitationService::CavitationService(const ModelConfig& config)
    : detector_(config.ae_input_dim, CavitationDetector::Thresholds{
          config.adaptive_base_incipient,
          config.adaptive_base_critical,
          config.adaptive_base_developed,
          config.adaptive_base_incipient,
          config.adaptive_base_critical,
          config.adaptive_base_developed
      }),
      detect_count_(0),
      last_log_count_(0) {}

bool CavitationService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    thread_ = std::thread(&CavitationService::processingLoop, this);
    spdlog::info("CavitationService: started (IF+AE ensemble, adaptive threshold)");
    return true;
}

void CavitationService::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    spdlog::info("CavitationService: stopped (total detections={})", detect_count_.load());
}

bool CavitationService::isRunning() {
    return running_.load();
}

void CavitationService::trainModel(const std::vector<std::vector<float>>& data, int epochs) {
    spdlog::info("CavitationService: retraining model with {} samples, {} epochs", data.size(), epochs);
    detector_.train(data, epochs);
    spdlog::info("CavitationService: model retraining complete");
}

void CavitationService::processingLoop() {
    auto last_log_time = std::chrono::steady_clock::now();
    while (running_.load()) {
        CollectorOutput collector_out;
        if (input_queue.tryPop(collector_out)) {
            OperatingCondition op_cond{};
            CavitationStatus cavitation = detector_.detect(collector_out.feature, op_cond);
            DetectorOutput det_out{cavitation, collector_out.feature, collector_out.raw_data};
            output_queue.tryPush(det_out);

            if (cavitation.stage != CavitationStage::NONE) {
                detect_count_.fetch_add(1, std::memory_order_relaxed);
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time);
            if (elapsed.count() >= 30) {
                size_t current = detect_count_.load(std::memory_order_relaxed);
                size_t delta = current - last_log_count_.exchange(current);
                spdlog::info("CavitationService: detected {} cavitation events in last 30s (total={})", delta, current);
                last_log_time = now;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}
