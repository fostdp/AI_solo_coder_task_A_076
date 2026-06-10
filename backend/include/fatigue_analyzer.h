#pragma once

#include <cstddef>
#include <vector>

#include "types.h"

struct RainflowCycle {
    float range;
    float mean;
    float count;
};

class RainflowCounter {
public:
    RainflowCounter() = default;
    ~RainflowCounter() = default;

    RainflowCounter(const RainflowCounter&) = delete;
    RainflowCounter& operator=(const RainflowCounter&) = delete;

    std::vector<RainflowCycle> process(const std::vector<float>& stress_history);

private:
    struct Point {
        float value;
        bool reserved;
    };
};

struct SNCurve {
    static constexpr int NUM_POINTS = 8;
    double N[NUM_POINTS] = {1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
    double S[NUM_POINTS] = {620.0, 480.0, 380.0, 300.0, 240.0, 200.0, 180.0, 170.0};
};

class FatigueAnalyzer {
public:
    FatigueAnalyzer() = default;
    ~FatigueAnalyzer() = default;

    FatigueAnalyzer(const FatigueAnalyzer&) = delete;
    FatigueAnalyzer& operator=(const FatigueAnalyzer&) = delete;

    double computeMinerDamage(const std::vector<RainflowCycle>& cycles, const SNCurve& sn_curve);

    double computeRemainingLife(double cumulative_damage,
                                double design_life_hours,
                                double elapsed_hours);

    FatigueDamage analyze(uint8_t turbine_id,
                          uint8_t blade_id,
                          uint8_t zone_id,
                          const std::vector<float>& stress_history,
                          float cavitation_intensity);

    void setCavitationFactor(float factor);
    void setDesignLifeHours(double hours);

private:
    double interpolateSN(double stress_range, const SNCurve& sn_curve);

    SNCurve sn_curve_{};
    float cavitation_factor_{1.5f};
    double design_life_hours_{100000.0};
};
