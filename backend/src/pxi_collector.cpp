#include "pxi_collector.h"

PXICollector::PXICollector(uint16_t port, const ModelConfig& config)
    : port_(port), config_(config), signal_processor_() {
    signal_processor_.setSampleRate(config_.sample_rate);
}

bool PXICollector::start() {
    udp_receiver_ = std::make_unique<UdpReceiver>(port_);
    udp_receiver_->setCallback([this](const SensorData& data) {
        onDataReceived(data);
    });

    if (!udp_receiver_->start()) {
        return false;
    }

    running_.store(true, std::memory_order_release);
    return true;
}

void PXICollector::stop() {
    if (udp_receiver_) {
        udp_receiver_->stop();
    }
    running_.store(false, std::memory_order_release);
}

bool PXICollector::isRunning() {
    return running_.load(std::memory_order_acquire);
}

size_t PXICollector::getDroppedPackets() {
    if (udp_receiver_) {
        return udp_receiver_->getDroppedPackets() + dropped_count_.load(std::memory_order_relaxed);
    }
    return dropped_count_.load(std::memory_order_relaxed);
}

size_t PXICollector::getReceivedPackets() {
    if (udp_receiver_) {
        return udp_receiver_->getReceivedPackets();
    }
    return 0;
}

void PXICollector::onDataReceived(const SensorData& data) {
    SpectrumFeature feature = signal_processor_.computeSpectralFeatures(data);
    CollectorOutput out{data, std::move(feature)};
    if (!output_queue.tryPush(out)) {
        dropped_count_.fetch_add(1, std::memory_order_relaxed);
    }
}
