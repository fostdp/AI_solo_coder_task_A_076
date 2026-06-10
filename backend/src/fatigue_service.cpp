#include "fatigue_service.h"

#include <chrono>
#include <cmath>
#include <numeric>

#include <spdlog/spdlog.h>

FatigueService::FatigueService(const ModelConfig& config)
    : cycle_count_(0), last_log_count_(0) {
    analyzer_.setCavitationFactor(config.cavitation_factor);
    analyzer_.setDesignLifeHours(config.design_life_hours);
}

bool FatigueService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    thread_ = std::thread(&FatigueService::processingLoop, this);
    spdlog::info("FatigueService: started (streaming rainflow + Miner, design_life={:.0f}h)",
        analyzer_.getDesignLifeHours());
    return true;
}

void FatigueService::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    spdlog::info("FatigueService: stopped (total cycles analyzed={})", cycle_count_.load());
}

bool FatigueService::isRunning() {
    return running_.load();
}

void FatigueService::processingLoop() {
    auto last_log_time = std::chrono::steady_clock::now();
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
                cycle_count_.fetch_add(1, std::memory_order_relaxed);
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

            if (fatigue.cumulative_damage > 0.5 && fatigue.turbine_id > 0) {
                spdlog::warn("FatigueService: turbine={} blade={} damage={:.2f}% remaining={:.0f}h",
                    fatigue.turbine_id, fatigue.blade_id,
                    fatigue.cumulative_damage * 100, fatigue.remaining_life_hours);
            }

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log_time);
            if (elapsed.count() >= 60) {
                size_t current = cycle_count_.load(std::memory_order_relaxed);
                size_t delta = current - last_log_count_.exchange(current);
                spdlog::info("FatigueService: {} cycles analyzed in last 60s (total={})", delta, current);
                last_log_time = now;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}
