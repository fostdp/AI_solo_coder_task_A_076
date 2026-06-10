#include "common.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

ModelConfig ModelConfig::loadFromFile(const std::string& path) {
    ModelConfig cfg;

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        std::cerr << "Warning: config file not found: " << path
                  << ", using defaults" << std::endl;
        return cfg;
    }

    try {
        json j = json::parse(ifs);

        if (j.contains("isolation_forest")) {
            auto& ifj = j["isolation_forest"];
            if (ifj.contains("n_trees")) cfg.if_n_trees = ifj["n_trees"];
            if (ifj.contains("sample_size")) cfg.if_sample_size = ifj["sample_size"];
        }

        if (j.contains("autoencoder")) {
            auto& aej = j["autoencoder"];
            if (aej.contains("input_dim")) cfg.ae_input_dim = aej["input_dim"];
            if (aej.contains("encoding_dim")) cfg.ae_encoding_dim = aej["encoding_dim"];
            if (aej.contains("learning_rate")) cfg.ae_learning_rate = aej["learning_rate"];
            if (aej.contains("train_epochs")) cfg.ae_train_epochs = aej["train_epochs"];
            if (aej.contains("warmstart_epochs")) cfg.ae_warmstart_epochs = aej["warmstart_epochs"];
            if (aej.contains("warmstart_lr_factor")) cfg.ae_warmstart_lr_factor = aej["warmstart_lr_factor"];
        }

        if (j.contains("ensemble")) {
            auto& ej = j["ensemble"];
            if (ej.contains("if_weight")) cfg.ensemble_if_weight = ej["if_weight"];
            if (ej.contains("ae_weight")) cfg.ensemble_ae_weight = ej["ae_weight"];
        }

        if (j.contains("adaptive_threshold")) {
            auto& atj = j["adaptive_threshold"];
            if (atj.contains("base_incipient")) cfg.adaptive_base_incipient = atj["base_incipient"];
            if (atj.contains("base_critical")) cfg.adaptive_base_critical = atj["base_critical"];
            if (atj.contains("base_developed")) cfg.adaptive_base_developed = atj["base_developed"];
            if (atj.contains("window_size")) cfg.adaptive_window_size = atj["window_size"];
            if (atj.contains("max_offset")) cfg.adaptive_max_offset = atj["max_offset"];
        }

        if (j.contains("condition_normalization")) {
            auto& cnj = j["condition_normalization"];
            if (cnj.contains("shift_tolerance")) cfg.cond_shift_tolerance = cnj["shift_tolerance"];
            if (cnj.contains("warmstart_pool_size")) cfg.warmstart_pool_size = cnj["warmstart_pool_size"];
            if (cnj.contains("history_size")) cfg.normalizer_history_size = cnj["history_size"];
            if (cnj.contains("head_tolerance")) cfg.normalizer_head_tolerance = cnj["head_tolerance"];
            if (cnj.contains("power_tolerance")) cfg.normalizer_power_tolerance = cnj["power_tolerance"];
        }

        if (j.contains("fatigue")) {
            auto& fj = j["fatigue"];
            if (fj.contains("cavitation_factor")) cfg.cavitation_factor = fj["cavitation_factor"];
            if (fj.contains("design_life_hours")) cfg.design_life_hours = fj["design_life_hours"];
        }

        if (j.contains("sn_curve")) {
            auto& snj = j["sn_curve"];
            if (snj.contains("N") && snj["N"].is_array()) {
                auto arr = snj["N"];
                for (int i = 0; i < cfg.sn_curve.num_points && i < static_cast<int>(arr.size()); i++) {
                    cfg.sn_curve.N[i] = arr[i];
                }
            }
            if (snj.contains("S") && snj["S"].is_array()) {
                auto arr = snj["S"];
                for (int i = 0; i < cfg.sn_curve.num_points && i < static_cast<int>(arr.size()); i++) {
                    cfg.sn_curve.S[i] = arr[i];
                }
            }
        }

        if (j.contains("signal_processing")) {
            auto& spj = j["signal_processing"];
            if (spj.contains("sample_rate")) cfg.sample_rate = spj["sample_rate"];
            if (spj.contains("fft_size")) cfg.fft_size = spj["fft_size"];
            if (spj.contains("wavelet_levels")) cfg.wavelet_levels = spj["wavelet_levels"];
        }

        if (j.contains("alarm_thresholds")) {
            auto& aj = j["alarm_thresholds"];
            if (aj.contains("cavitation_intensity_limit")) cfg.alarm_cavitation_intensity_limit = aj["cavitation_intensity_limit"];
            if (aj.contains("cavitation_developed_limit")) cfg.alarm_cavitation_developed_limit = aj["cavitation_developed_limit"];
            if (aj.contains("vibration_velocity_limit")) cfg.alarm_vibration_velocity_limit = aj["vibration_velocity_limit"];
            if (aj.contains("fatigue_damage_limit")) cfg.alarm_fatigue_damage_limit = aj["fatigue_damage_limit"];
        }

        if (j.contains("server")) {
            auto& sj = j["server"];
            if (sj.contains("udp_port")) cfg.udp_port = sj["udp_port"];
            if (sj.contains("api_port")) cfg.api_port = sj["api_port"];
            if (sj.contains("clickhouse_connection")) cfg.clickhouse_connection = sj["clickhouse_connection"];
        }

        std::cout << "Config loaded from: " << path << std::endl;
    } catch (const json::exception& e) {
        std::cerr << "Error parsing config: " << e.what() << std::endl;
    }

    return cfg;
}
