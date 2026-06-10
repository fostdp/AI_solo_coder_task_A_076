#include "fatigue_analyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

bool StreamingRainflowCounter::tryExtractCycle() {
    if (residual_.size() < 4) return false;

    size_t n = residual_.size();
    float y1 = std::abs(residual_[n - 2] - residual_[n - 3]);
    float y2 = std::abs(residual_[n - 1] - residual_[n - 2]);
    float y3 = std::abs(residual_[n]     - residual_[n - 1]);

    if (y2 <= y1 && y2 <= y3) {
        RainflowCycle cycle;
        cycle.range = y2;
        cycle.mean = (residual_[n - 2] + residual_[n - 1]) / 2.0f;
        cycle.count = 1.0f;

        completed_cycles_.push_back(cycle);
        total_cycle_count_++;

        if (sn_curve_ptr_) {
            double ni = 1.0;
            const SNCurve& sn = *sn_curve_ptr_;
            double stress_range = static_cast<double>(cycle.range);
            if (stress_range >= sn.S[0]) {
                ni = sn.N[0];
            } else if (stress_range <= sn.S[SNCurve::NUM_POINTS - 1]) {
                ni = sn.N[SNCurve::NUM_POINTS - 1];
            } else {
                for (int i = 0; i < SNCurve::NUM_POINTS - 1; i++) {
                    if (stress_range <= sn.S[i] && stress_range > sn.S[i + 1]) {
                        double log_s = std::log10(stress_range);
                        double log_s0 = std::log10(sn.S[i]);
                        double log_s1 = std::log10(sn.S[i + 1]);
                        double log_n0 = std::log10(sn.N[i]);
                        double log_n1 = std::log10(sn.N[i + 1]);
                        double t = (log_s - log_s0) / (log_s1 - log_s0);
                        ni = std::pow(10.0, log_n0 + t * (log_n1 - log_n0));
                        break;
                    }
                }
            }
            if (ni > 0.0) {
                accumulated_damage_ += static_cast<double>(cycle.count) / ni;
            }
        }

        residual_.erase(residual_.end() - 2);
        return true;
    }

    return false;
}

void StreamingRainflowCounter::feed(float stress_value) {
    if (first_value_) {
        prev_extreme_ = stress_value;
        direction_ = 0.0f;
        first_value_ = false;
        residual_.push_back(stress_value);
        return;
    }

    float new_direction = stress_value - prev_extreme_;

    if (new_direction * direction_ < 0.0f) {
        residual_.push_back(prev_extreme_);

        while (tryExtractCycle()) {}

        direction_ = new_direction;
    }

    if (direction_ == 0.0f || std::abs(new_direction) > std::abs(direction_)) {
        direction_ = new_direction;
    }

    prev_extreme_ = stress_value;

    if (residual_.size() > MAX_RESIDUAL) {
        float oldest = residual_[0];
        residual_.erase(residual_.begin());

        if (residual_.size() >= 2) {
            RainflowCycle half_cycle;
            half_cycle.range = std::abs(residual_[0] - oldest);
            half_cycle.mean = (residual_[0] + oldest) / 2.0f;
            half_cycle.count = 0.5f;
            completed_cycles_.push_back(half_cycle);
            total_cycle_count_++;

            if (sn_curve_ptr_) {
                double ni = 1.0;
                const SNCurve& sn = *sn_curve_ptr_;
                double sr = static_cast<double>(half_cycle.range);
                if (sr >= sn.S[0]) ni = sn.N[0];
                else if (sr <= sn.S[SNCurve::NUM_POINTS - 1]) ni = sn.N[SNCurve::NUM_POINTS - 1];
                else {
                    for (int i = 0; i < SNCurve::NUM_POINTS - 1; i++) {
                        if (sr <= sn.S[i] && sr > sn.S[i + 1]) {
                            double log_s = std::log10(sr);
                            double log_s0 = std::log10(sn.S[i]);
                            double log_s1 = std::log10(sn.S[i + 1]);
                            double log_n0 = std::log10(sn.N[i]);
                            double log_n1 = std::log10(sn.N[i + 1]);
                            double t = (log_s - log_s0) / (log_s1 - log_s0);
                            ni = std::pow(10.0, log_n0 + t * (log_n1 - log_n0));
                            break;
                        }
                    }
                }
                if (ni > 0.0) accumulated_damage_ += 0.5 / ni;
            }
        }
    }
}

std::vector<RainflowCycle> StreamingRainflowCounter::extractCycles() const {
    return completed_cycles_;
}

void StreamingRainflowCounter::reset() {
    residual_.clear();
    completed_cycles_.clear();
    prev_extreme_ = 0.0f;
    direction_ = 0.0f;
    first_value_ = true;
    total_cycle_count_ = 0;
    accumulated_damage_ = 0.0;
}

double FatigueAnalyzer::interpolateSN(double stress_range, const SNCurve& sn_curve) {
    if (stress_range >= sn_curve.S[0]) return sn_curve.N[0];
    if (stress_range <= sn_curve.S[SNCurve::NUM_POINTS - 1]) return sn_curve.N[SNCurve::NUM_POINTS - 1];

    for (int i = 0; i < SNCurve::NUM_POINTS - 1; i++) {
        if (stress_range <= sn_curve.S[i] && stress_range > sn_curve.S[i + 1]) {
            double log_s = std::log10(stress_range);
            double log_s0 = std::log10(sn_curve.S[i]);
            double log_s1 = std::log10(sn_curve.S[i + 1]);
            double log_n0 = std::log10(sn_curve.N[i]);
            double log_n1 = std::log10(sn_curve.N[i + 1]);

            double t = (log_s - log_s0) / (log_s1 - log_s0);
            double log_n = log_n0 + t * (log_n1 - log_n0);
            return std::pow(10.0, log_n);
        }
    }
    return sn_curve.N[SNCurve::NUM_POINTS - 1];
}

double FatigueAnalyzer::computeMinerDamage(const std::vector<RainflowCycle>& cycles,
                                            const SNCurve& sn_curve) {
    double damage = 0.0;
    for (const auto& cycle : cycles) {
        double ni = interpolateSN(static_cast<double>(cycle.range), sn_curve);
        if (ni > 0.0) {
            damage += static_cast<double>(cycle.count) / ni;
        }
    }
    return damage;
}

double FatigueAnalyzer::computeRemainingLife(double cumulative_damage,
                                              double design_life_hours,
                                              double elapsed_hours) {
    if (cumulative_damage >= 1.0) return 0.0;
    if (elapsed_hours <= 0.0) return design_life_hours * (1.0 - cumulative_damage);
    return elapsed_hours * (1.0 - cumulative_damage) / cumulative_damage;
}

FatigueAnalyzer::BladeState& FatigueAnalyzer::getBladeState(uint8_t turbine_id,
                                                             uint8_t blade_id,
                                                             uint8_t zone_id) {
    uint32_t key = makeKey(turbine_id, blade_id, zone_id);

    for (size_t i = 0; i < blade_keys_.size(); i++) {
        if (blade_keys_[i] == key) {
            return blade_states_[i];
        }
    }

    blade_keys_.push_back(key);
    blade_states_.push_back(BladeState{});
    blade_states_.back().counter.setSNCurveForOnlineDamage(&sn_curve_);
    return blade_states_.back();
}

FatigueDamage FatigueAnalyzer::analyze(uint8_t turbine_id,
                                        uint8_t blade_id,
                                        uint8_t zone_id,
                                        const std::vector<float>& stress_history,
                                        float cavitation_intensity) {
    FatigueDamage result;
    result.turbine_id = turbine_id;
    result.blade_id = blade_id;
    result.zone_id = zone_id;

    if (stress_history.empty()) {
        result.timestamp_ms = 0;
        result.stress_amplitude = 0.0f;
        result.stress_mean = 0.0f;
        result.cycle_count = 0;
        result.miner_damage = 0.0;
        result.cumulative_damage = 0.0;
        result.remaining_life_hours = design_life_hours_;
        return result;
    }

    BladeState& state = getBladeState(turbine_id, blade_id, zone_id);

    float amplification = 1.0f + cavitation_intensity * (cavitation_factor_ - 1.0f);

    float mean_val = 0.0f;
    for (size_t i = 0; i < stress_history.size(); i++) {
        mean_val += stress_history[i];
    }
    mean_val /= static_cast<float>(stress_history.size());

    float running_max = -1e30f;
    float running_min = 1e30f;

    for (size_t i = 0; i < stress_history.size(); i++) {
        float amplified = mean_val + (stress_history[i] - mean_val) * amplification;
        state.counter.feed(amplified);
        state.sample_count++;

        if (amplified > running_max) running_max = amplified;
        if (amplified < running_min) running_min = amplified;
    }

    state.stress_amplitude = (running_max - running_min) / 2.0f;
    state.stress_mean = (running_max + running_min) / 2.0f;
    state.elapsed_hours = static_cast<double>(state.sample_count) / 50000.0 / 3600.0;
    state.cumulative_damage = state.counter.getAccumulatedDamage();

    result.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    result.stress_amplitude = state.stress_amplitude;
    result.stress_mean = state.stress_mean;
    result.cycle_count = state.counter.getCycleCount();
    result.miner_damage = state.counter.getAccumulatedDamage();
    result.cumulative_damage = state.cumulative_damage;
    result.remaining_life_hours = computeRemainingLife(
        state.cumulative_damage, design_life_hours_, state.elapsed_hours);

    return result;
}

FatigueDamage FatigueAnalyzer::analyzeIncremental(uint8_t turbine_id,
                                                    uint8_t blade_id,
                                                    uint8_t zone_id,
                                                    float new_stress_value,
                                                    float cavitation_intensity) {
    BladeState& state = getBladeState(turbine_id, blade_id, zone_id);

    float amplification = 1.0f + cavitation_intensity * (cavitation_factor_ - 1.0f);
    float amplified = new_stress_value * amplification;

    state.counter.feed(amplified);
    state.sample_count++;
    state.cumulative_damage = state.counter.getAccumulatedDamage();

    if (amplified > state.stress_mean + state.stress_amplitude) {
        state.stress_amplitude = amplified - state.stress_mean;
    }
    if (amplified < state.stress_mean - state.stress_amplitude) {
        state.stress_amplitude = state.stress_mean - amplified;
    }

    state.elapsed_hours = static_cast<double>(state.sample_count) / 50000.0 / 3600.0;

    FatigueDamage result;
    result.turbine_id = turbine_id;
    result.blade_id = blade_id;
    result.zone_id = zone_id;
    result.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    result.stress_amplitude = state.stress_amplitude;
    result.stress_mean = state.stress_mean;
    result.cycle_count = state.counter.getCycleCount();
    result.miner_damage = state.counter.getAccumulatedDamage();
    result.cumulative_damage = state.cumulative_damage;
    result.remaining_life_hours = computeRemainingLife(
        state.cumulative_damage, design_life_hours_, state.elapsed_hours);

    return result;
}

void FatigueAnalyzer::resetAccumulated(uint8_t turbine_id, uint8_t blade_id, uint8_t zone_id) {
    BladeState& state = getBladeState(turbine_id, blade_id, zone_id);
    state.counter.reset();
    state.cumulative_damage = 0.0;
    state.elapsed_hours = 0.0;
    state.sample_count = 0;
    state.stress_amplitude = 0.0f;
    state.stress_mean = 0.0f;
}

void FatigueAnalyzer::setCavitationFactor(float factor) {
    cavitation_factor_ = factor;
}

void FatigueAnalyzer::setDesignLifeHours(double hours) {
    design_life_hours_ = hours;
}
