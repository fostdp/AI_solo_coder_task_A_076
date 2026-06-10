#include "fatigue_service.h"

#include <chrono>
#include <cmath>
#include <numeric>

FatigueService::FatigueService(const ModelConfig& config) {
    analyzer_.setCavitationFactor(config.cavitation_factor);
    analyzer_.setDesignLifeHours(config.design_life_hours);
}

bool FatigueService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    thread_ = std::thread(&FatigueService::processingLoop, this);
    return true;
}

void FatigueService::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool FatigueService::isRunning() {
    return running_.load();
}

void FatigueService::processingLoop() {
    while (running_.load()) {
        DetectorOutput det_out;
        if (input_queue.tryPop(det_out)) {
            std::vector<float> stress;
            if (det_out.raw_data.sensor_type == SensorType::ACCELEROMETER) {
                stress = det_out.raw_data.samples;
            }

            FatigueDamage fatigue{};
            if (!stress.empty()) {
                fatigue = analyzer_.analyze(
                    det_out.cavitation.turbine_id,
                    det_out.cavitation.blade_id,
                    det_out.cavitation.zone_id,
                    stress,
                    det_out.cavitation.intensity);
            }

            float vibration_velocity = 0.0f;
            if (!det_out.raw_data.samples.empty()) {
                float sum_sq = std::accumulate(
                    det_out.raw_data.samples.begin(),
                    det_out.raw_data.samples.end(),
                    0.0f,
                    [](float acc, float s) { return acc + s * s; });
                vibration_velocity = std::sqrt(sum_sq / static_cast<float>(det_out.raw_data.samples.size()));
            }

            EvaluatorOutput eval_out{det_out.cavitation, fatigue, vibration_velocity};
            output_queue.tryPush(eval_out);
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}
