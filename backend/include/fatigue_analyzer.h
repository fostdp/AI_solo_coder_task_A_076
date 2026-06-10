#pragma once

#include <cstddef>
#include <vector>

#include "common.h"

struct RainflowCycle {
    float range;
    float mean;
    float count;
};

class StreamingRainflowCounter {
public:
    StreamingRainflowCounter() = default;
    ~StreamingRainflowCounter() = default;

    StreamingRainflowCounter(const StreamingRainflowCounter&) = delete;
    StreamingRainflowCounter& operator=(const StreamingRainflowCounter&) = delete;

    void feed(float stress_value);

    std::vector<RainflowCycle> extractCycles() const;
    void reset();

    size_t getCycleCount() const { return total_cycle_count_; }
    double getAccumulatedDamage() const { return accumulated_damage_; }

    void setSNCurveForOnlineDamage(const struct SNCurve* sn) { sn_curve_ptr_ = sn; }

private:
    static constexpr size_t MAX_RESIDUAL = 20;

    std::vector<float> residual_;
    float prev_extreme_{0.0f};
    float direction_{0.0f};
    bool first_value_{true};

    std::vector<RainflowCycle> completed_cycles_;
    size_t total_cycle_count_{0};
    double accumulated_damage_{0.0};
    const struct SNCurve* sn_curve_ptr_{nullptr};

    bool tryExtractCycle();
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

    FatigueDamage analyzeIncremental(uint8_t turbine_id,
                                      uint8_t blade_id,
                                      uint8_t zone_id,
                                      float new_stress_value,
                                      float cavitation_intensity);

    void resetAccumulated(uint8_t turbine_id, uint8_t blade_id, uint8_t zone_id);
    void setCavitationFactor(float factor);
    void setDesignLifeHours(double hours);

private:
    double interpolateSN(double stress_range, const SNCurve& sn_curve);

    SNCurve sn_curve_{};
    float cavitation_factor_{1.5f};
    double design_life_hours_{100000.0};

    struct BladeState {
        StreamingRainflowCounter counter;
        double cumulative_damage{0.0};
        double elapsed_hours{0.0};
        uint64_t sample_count{0};
        float stress_amplitude{0.0f};
        float stress_mean{0.0f};
    };

    BladeState& getBladeState(uint8_t turbine_id, uint8_t blade_id, uint8_t zone_id);

    std::vector<BladeState> blade_states_;
    std::vector<uint32_t> blade_keys_;

    uint32_t makeKey(uint8_t t, uint8_t b, uint8_t z) const {
        return (static_cast<uint32_t>(t) << 16) |
               (static_cast<uint32_t>(b) << 8) |
               static_cast<uint32_t>(z);
    }
};
