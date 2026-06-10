#pragma once

#include <atomic>
#include <thread>

#include "common.h"
#include "fatigue_analyzer.h"

class FatigueService {
public:
    explicit FatigueService(const ModelConfig& config);

    bool start();
    void stop();
    bool isRunning();

    MPSCQueue<DetectorOutput, 8192> input_queue;
    MPSCQueue<EvaluatorOutput, 8192> output_queue;

private:
    void processingLoop();

    FatigueAnalyzer analyzer_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};
