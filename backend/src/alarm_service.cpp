#include "alarm_service.h"

#include <chrono>

#include <spdlog/spdlog.h>

AlarmService::AlarmService(const ModelConfig& config)
    : alarm_manager_(AlarmThreshold{
          config.alarm_cavitation_intensity_limit,
          config.alarm_cavitation_developed_limit,
          config.alarm_vibration_velocity_limit,
          config.alarm_fatigue_damage_limit}),
      clickhouse_writer_(config.clickhouse_connection),
      config_(config),
      alarm_count_(0),
      iec_sent_count_(0) {}

bool AlarmService::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return false;
    }
    if (!clickhouse_writer_.start()) {
        spdlog::error("AlarmService: failed to start ClickHouse writer ({})",
            "connection error");
        running_.store(false);
        return false;
    }
    thread_ = std::thread(&AlarmService::processingLoop, this);
    spdlog::info("AlarmService: started (CH={}, IEC 61850, limits: cav={:.1f}/dev={:.1f}/vib={:.1f}/fat={:.1f})",
        "connected",
        config_.alarm_cavitation_intensity_limit,
        config_.alarm_cavitation_developed_limit,
        config_.alarm_vibration_velocity_limit,
        config_.alarm_fatigue_damage_limit);
    return true;
}

void AlarmService::stop() {
    clickhouse_writer_.stop();
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    spdlog::info("AlarmService: stopped (alarms={}, iec61850_sent={})",
        alarm_count_.load(), iec_sent_count_.load());
}

bool AlarmService::isRunning() {
    return running_.load();
}

void AlarmService::processingLoop() {
    while (running_.load()) {
        EvaluatorOutput eval_out;
        if (input_queue.tryPop(eval_out)) {
            clickhouse_writer_.writeCavitationStatus(eval_out.cavitation);
            clickhouse_writer_.writeFatigueDamage(eval_out.fatigue);

            std::vector<AlarmRecord> alarms = alarm_manager_.checkAlarms(
                eval_out.cavitation, eval_out.fatigue, eval_out.vibration_velocity);

            for (auto& alarm : alarms) {
                clickhouse_writer_.writeAlarmRecord(alarm);
                bool sent = alarm_manager_.sendIEC61850(alarm);
                alarm_count_.fetch_add(1, std::memory_order_relaxed);
                if (sent) {
                    iec_sent_count_.fetch_add(1, std::memory_order_relaxed);
                }

                const char* sev_str = alarm.severity == AlarmSeverity::CRITICAL ? "CRITICAL" :
                                      alarm.severity == AlarmSeverity::WARNING ? "WARNING" : "INFO";
                spdlog::warn("AlarmService: {} alarm turbine={} blade={} type={} iec61850={}",
                    sev_str, alarm.turbine_id, alarm.blade_id,
                    static_cast<int>(alarm.alarm_type), sent);
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                latest_cavitation_[eval_out.cavitation.turbine_id] = eval_out.cavitation;
                uint16_t fat_key = static_cast<uint16_t>(eval_out.fatigue.turbine_id) << 8 |
                                   eval_out.fatigue.blade_id;
                latest_fatigue_[fat_key] = eval_out.fatigue;
                turbine_ids_.insert(eval_out.cavitation.turbine_id);
                for (const auto& alarm : alarms) {
                    recent_alarms_.push_back(alarm);
                }
                if (recent_alarms_.size() > 1000) {
                    recent_alarms_.erase(recent_alarms_.begin(),
                                         recent_alarms_.begin() + (recent_alarms_.size() - 1000));
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

CavitationStatus AlarmService::getLatestCavitation(uint8_t turbine_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = latest_cavitation_.find(turbine_id);
    if (it != latest_cavitation_.end()) {
        return it->second;
    }
    return CavitationStatus{};
}

FatigueDamage AlarmService::getLatestFatigue(uint8_t turbine_id, uint8_t blade_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    uint16_t key = static_cast<uint16_t>(turbine_id) << 8 | blade_id;
    auto it = latest_fatigue_.find(key);
    if (it != latest_fatigue_.end()) {
        return it->second;
    }
    return FatigueDamage{};
}

std::vector<AlarmRecord> AlarmService::getRecentAlarms(AlarmSeverity min_severity) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<AlarmRecord> result;
    result.reserve(recent_alarms_.size());
    for (const auto& alarm : recent_alarms_) {
        if (static_cast<uint8_t>(alarm.severity) >= static_cast<uint8_t>(min_severity)) {
            result.push_back(alarm);
        }
    }
    return result;
}

std::vector<uint8_t> AlarmService::getTurbineList() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return std::vector<uint8_t>(turbine_ids_.begin(), turbine_ids_.end());
}
