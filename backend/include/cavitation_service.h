#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "cavitation_detector.h"
#include "common.h"

class CavitationService {
public:
    explicit CavitationService(const ModelConfig& config);

    bool start();
    void stop();
    bool isRunning();
    void trainModel(const std::vector<std::vector<float>>& data, int epochs);

    MPSCQueue<CollectorOutput, 8192> input_queue;
    MPSCQueue<DetectorOutput, 8192> output_queue;

private:
    void processingLoop();

    CavitationDetector detector_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> detect_count_{0};
    std::atomic<size_t> last_log_count_{0};
};
