#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "common.h"
#include "signal_processor.h"
#include "udp_receiver.h"

class PXICollector {
public:
    MPSCQueue<CollectorOutput, 8192> output_queue;

    PXICollector(uint16_t port, const ModelConfig& config);
    ~PXICollector() = default;

    PXICollector(const PXICollector&) = delete;
    PXICollector& operator=(const PXICollector&) = delete;

    bool start();
    void stop();
    bool isRunning();
    size_t getDroppedPackets();
    size_t getReceivedPackets();

private:
    void onDataReceived(const SensorData& data);

    uint16_t port_;
    ModelConfig config_;
    SignalProcessor signal_processor_;
    std::unique_ptr<UdpReceiver> udp_receiver_;

    std::atomic<bool> running_{false};
    std::atomic<size_t> dropped_count_{0};
};
