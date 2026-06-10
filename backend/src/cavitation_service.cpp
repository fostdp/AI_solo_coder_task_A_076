#include "cavitation_service.h"

#include <chrono>

CavitationService::CavitationService(const ModelConfig& config)
    : detector_(config.ae_input_dim, CavitationDetector::Thresholds{
          config.adaptive_base_incipient,
          config.adaptive_base_critical,
          config.adaptive_base_developed,
          config.adaptive_base_incipient,
          config.adaptive_base_critical,
          config.adaptive_base_developed
      }) {}

bool CavitationService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    thread_ = std::thread(&CavitationService::processingLoop, this);
    return true;
}

void CavitationService::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool CavitationService::isRunning() {
    return running_.load();
}

void CavitationService::trainModel(const std::vector<std::vector<float>>& data, int epochs) {
    detector_.train(data, epochs);
}

void CavitationService::processingLoop() {
    while (running_.load()) {
        CollectorOutput collector_out;
        if (input_queue.tryPop(collector_out)) {
            OperatingCondition op_cond{};
            CavitationStatus cavitation = detector_.detect(collector_out.feature, op_cond);
            DetectorOutput det_out{cavitation, collector_out.feature, collector_out.raw_data};
            output_queue.tryPush(det_out);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}
