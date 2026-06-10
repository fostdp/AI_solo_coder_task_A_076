#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "alarm_manager.h"
#include "clickhouse_writer.h"
#include "common.h"

class AlarmService {
public:
    explicit AlarmService(const ModelConfig& config);

    bool start();
    void stop();
    bool isRunning();

    MPSCQueue<EvaluatorOutput, 8192> input_queue;

    CavitationStatus getLatestCavitation(uint8_t turbine_id);
    FatigueDamage getLatestFatigue(uint8_t turbine_id, uint8_t blade_id);
    std::vector<AlarmRecord> getRecentAlarms(AlarmSeverity min_severity);
    std::vector<uint8_t> getTurbineList();

private:
    void processingLoop();

    AlarmManager alarm_manager_;
    ClickHouseWriter clickhouse_writer_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex state_mutex_;
    std::unordered_map<uint8_t, CavitationStatus> latest_cavitation_;
    std::unordered_map<uint16_t, FatigueDamage> latest_fatigue_;
    std::vector<AlarmRecord> recent_alarms_;
    std::unordered_set<uint8_t> turbine_ids_;
};
