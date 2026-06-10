#pragma once

#include <string>
#include <vector>

#include "common.h"

struct AlarmThreshold {
    double cavitation_intensity_limit{0.5};
    double vibration_velocity_limit{4.5};
    double fatigue_damage_limit{0.8};
    double cavitation_developed_limit{0.7};
};

class AlarmManager {
public:
    explicit AlarmManager(const AlarmThreshold& thresholds = AlarmThreshold{});
    ~AlarmManager() = default;

    AlarmManager(const AlarmManager&) = delete;
    AlarmManager& operator=(const AlarmManager&) = delete;

    std::vector<AlarmRecord> checkAlarms(const CavitationStatus& cavitation,
                                          const FatigueDamage& fatigue,
                                          float vibration_velocity);

    bool sendIEC61850(const AlarmRecord& alarm);

    std::string generateMaintenanceAdvice(const AlarmRecord& alarm);

    void setThresholds(const AlarmThreshold& thresholds);
    const AlarmThreshold& getThresholds() const;

private:
    AlarmThreshold thresholds_;

    std::string formatIEC61850Report(const AlarmRecord& alarm);
};
