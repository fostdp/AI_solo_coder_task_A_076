#include "fatigue_analyzer.h"

#include <algorithm>
#include <cmath>
#include <vector>

std::vector<RainflowCycle> RainflowCounter::process(const std::vector<float>& stress_history) {
    std::vector<RainflowCycle> cycles;
    if (stress_history.size() < 4) return cycles;

    std::vector<Point> peaks;
    for (size_t i = 0; i < stress_history.size(); i++) {
        if (i == 0 || i == stress_history.size() - 1) {
            peaks.push_back({stress_history[i], false});
        } else {
            bool is_peak = (stress_history[i] > stress_history[i - 1] &&
                            stress_history[i] >= stress_history[i + 1]) ||
                           (stress_history[i] < stress_history[i - 1] &&
                            stress_history[i] <= stress_history[i + 1]);
            if (is_peak) {
                if (!peaks.empty() && peaks.back().value == stress_history[i]) {
                    continue;
                }
                peaks.push_back({stress_history[i], false});
            }
        }
    }

    std::vector<Point> stack;
    for (auto& pt : peaks) {
        stack.push_back(pt);

        while (stack.size() >= 4) {
            size_t n = stack.size();
            float s1 = std::abs(stack[n - 2].value - stack[n - 3].value);
            float s2 = std::abs(stack[n - 1].value - stack[n - 2].value);
            float s3 = std::abs(stack[n].value - stack[n - 1].value);

            if (s2 <= s1 && s2 <= s3) {
                float range = s2;
                float mean = (stack[n - 2].value + stack[n - 1].value) / 2.0f;

                bool x_inner = stack[n - 2].reserved;
                bool y_inner = stack[n - 1].reserved;

                RainflowCycle cycle;
                cycle.range = range;
                cycle.mean = mean;

                if (!x_inner && !y_inner) {
                    cycle.count = 1.0f;
                } else if (x_inner != y_inner) {
                    cycle.count = 0.5f;
                } else {
                    cycle.count = 0.5f;
                }

                cycles.push_back(cycle);

                stack[n - 3].reserved = stack[n - 3].reserved || stack[n - 2].reserved;
                stack[n].reserved = stack[n].reserved || stack[n - 1].reserved;

                stack.erase(stack.begin() + n - 2);
                stack.erase(stack.begin() + n - 2);
            } else {
                break;
            }
        }
    }

    size_t n = stack.size();
    for (size_t i = 0; i + 1 < n; i++) {
        RainflowCycle cycle;
        cycle.range = std::abs(stack[i + 1].value - stack[i].value);
        cycle.mean = (stack[i + 1].value + stack[i].value) / 2.0f;
        cycle.count = 0.5f;
        cycles.push_back(cycle);
    }

    return cycles;
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

    result.timestamp_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    float amplification = 1.0f + cavitation_intensity * (cavitation_factor_ - 1.0f);
    std::vector<float> amplified(stress_history.size());
    float mean_val = 0.0f;
    for (size_t i = 0; i < stress_history.size(); i++) {
        mean_val += stress_history[i];
    }
    mean_val /= static_cast<float>(stress_history.size());

    for (size_t i = 0; i < stress_history.size(); i++) {
        amplified[i] = mean_val + (stress_history[i] - mean_val) * amplification;
    }

    RainflowCounter counter;
    std::vector<RainflowCycle> cycles = counter.process(amplified);

    float max_range = 0.0f;
    float cycle_mean_sum = 0.0f;
    float cycle_count_sum = 0.0f;
    for (const auto& c : cycles) {
        if (c.range > max_range) max_range = c.range;
        cycle_mean_sum += c.mean * c.count;
        cycle_count_sum += c.count;
    }

    result.stress_amplitude = max_range / 2.0f;
    result.stress_mean = cycle_count_sum > 0.0f ? cycle_mean_sum / cycle_count_sum : mean_val;
    result.cycle_count = static_cast<uint64_t>(std::max(0.0f, cycle_count_sum + 0.5f));

    double incremental = computeMinerDamage(cycles, sn_curve_);
    result.miner_damage = incremental;
    result.cumulative_damage = incremental;

    double elapsed_hours = static_cast<double>(stress_history.size()) / 50000.0 / 3600.0;
    result.remaining_life_hours = computeRemainingLife(
        result.cumulative_damage, design_life_hours_, elapsed_hours);

    return result;
}

void FatigueAnalyzer::setCavitationFactor(float factor) {
    cavitation_factor_ = factor;
}

void FatigueAnalyzer::setDesignLifeHours(double hours) {
    design_life_hours_ = hours;
}
