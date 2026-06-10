#include "cavitation_detector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

static float harmonicNumber(float i) {
    return std::log(i) + 0.5772156649f;
}

static float computeC(size_t n) {
    if (n <= 1) return 0.0f;
    if (n == 2) return 1.0f;
    return 2.0f * (harmonicNumber(static_cast<float>(n) - 1.0f) -
                   static_cast<float>(n - 1) / static_cast<float>(n));
}

IsolationForest::IsolationForest(int n_trees, int sample_size)
    : n_trees_(n_trees), sample_size_(sample_size),
      max_depth_(static_cast<int>(std::ceil(std::log2(sample_size)))) {}

void IsolationForest::fit(const std::vector<std::vector<float>>& data) {
    training_size_ = data.size();
    trees_.clear();

    std::uniform_int_distribution<size_t> idx_dist(0, data.size() - 1);

    for (int t = 0; t < n_trees_; t++) {
        std::vector<std::vector<float>> subsample;
        if (static_cast<int>(data.size()) <= sample_size_) {
            subsample = data;
        } else {
            for (int i = 0; i < sample_size_; i++) {
                subsample.push_back(data[idx_dist(rng_)]);
            }
        }
        trees_.push_back(buildTree(subsample, 0, max_depth_));
    }
}

std::unique_ptr<IsolationTree> IsolationForest::buildTree(
    const std::vector<std::vector<float>>& data, int depth, int max_depth) {
    auto node = std::make_unique<IsolationTree>();
    node->depth = depth;

    if (depth >= max_depth || data.size() <= 1) {
        node->is_leaf = true;
        node->size = data.size();
        return node;
    }

    int num_features = static_cast<int>(data[0].size());
    std::uniform_int_distribution<int> feat_dist(0, num_features - 1);
    node->split_feature = feat_dist(rng_);

    float min_val = data[0][node->split_feature];
    float max_val = data[0][node->split_feature];
    for (const auto& row : data) {
        min_val = std::min(min_val, row[node->split_feature]);
        max_val = std::max(max_val, row[node->split_feature]);
    }

    if (min_val == max_val) {
        node->is_leaf = true;
        node->size = data.size();
        return node;
    }

    std::uniform_real_distribution<float> val_dist(min_val, max_val);
    node->split_value = val_dist(rng_);

    std::vector<std::vector<float>> left_data, right_data;
    for (const auto& row : data) {
        if (row[node->split_feature] < node->split_value) {
            left_data.push_back(row);
        } else {
            right_data.push_back(row);
        }
    }

    if (left_data.empty() || right_data.empty()) {
        node->is_leaf = true;
        node->size = data.size();
        return node;
    }

    node->left = buildTree(left_data, depth + 1, max_depth);
    node->right = buildTree(right_data, depth + 1, max_depth);
    return node;
}

float IsolationForest::pathLength(const IsolationTree* tree,
                                   const std::vector<float>& sample,
                                   int current_depth) {
    if (!tree) return static_cast<float>(current_depth);

    if (tree->is_leaf) {
        return static_cast<float>(current_depth) +
               (tree->size <= 1 ? 0.0f : computeC(tree->size));
    }

    if (sample[tree->split_feature] < tree->split_value) {
        return pathLength(tree->left.get(), sample, current_depth + 1);
    } else {
        return pathLength(tree->right.get(), sample, current_depth + 1);
    }
}

float IsolationForest::computeAnomalyScore(float path_len) {
    float c = computeC(static_cast<size_t>(training_size_));
    if (c == 0.0f) return 0.5f;
    return std::pow(2.0f, -path_len / c);
}

float IsolationForest::score(const std::vector<float>& sample) {
    if (trees_.empty()) return 0.5f;

    float total_path = 0.0f;
    for (const auto& tree : trees_) {
        total_path += pathLength(tree.get(), sample, 0);
    }
    float avg_path = total_path / static_cast<float>(trees_.size());
    return computeAnomalyScore(avg_path);
}

DeepAutoEncoder::DeepAutoEncoder(int input_dim, int encoding_dim, float learning_rate)
    : input_dim_(input_dim), encoding_dim_(encoding_dim), learning_rate_(learning_rate) {
    encoder_dims_ = {64, 32, encoding_dim};
    decoder_dims_ = {32, 64, input_dim};

    std::mt19937 gen(std::random_device{}());

    auto init_layer = [&](int in_size, int out_size) -> std::vector<std::vector<float>> {
        float scale = std::sqrt(2.0f / static_cast<float>(in_size));
        std::normal_distribution<float> dist(0.0f, scale);
        std::vector<std::vector<float>> w(out_size, std::vector<float>(in_size));
        for (auto& row : w) {
            for (auto& v : row) {
                v = dist(gen);
            }
        }
        return w;
    };

    encoder_weights_[0] = init_layer(input_dim_, encoder_dims_[0]);
    encoder_weights_[1] = init_layer(encoder_dims_[0], encoder_dims_[1]);
    encoder_weights_[2] = init_layer(encoder_dims_[1], encoder_dims_[2]);

    decoder_weights_[0] = init_layer(encoding_dim_, decoder_dims_[0]);
    decoder_weights_[1] = init_layer(decoder_dims_[0], decoder_dims_[1]);
    decoder_weights_[2] = init_layer(decoder_dims_[1], decoder_dims_[2]);

    for (int i = 0; i < 3; i++) {
        encoder_bias_[i].resize(encoder_dims_[i], 0.0f);
        decoder_bias_[i].resize(decoder_dims_[i], 0.0f);
    }
}

float DeepAutoEncoder::relu(float x) const {
    return x > 0.0f ? x : 0.0f;
}

float DeepAutoEncoder::reluDerivative(float x) const {
    return x > 0.0f ? 1.0f : 0.0f;
}

std::vector<float> DeepAutoEncoder::forward(const std::vector<float>& input) {
    std::vector<float> current = input;

    for (int layer = 0; layer < 3; layer++) {
        std::vector<float> output(encoder_dims_[layer], 0.0f);
        for (int j = 0; j < encoder_dims_[layer]; j++) {
            float sum = encoder_bias_[layer][j];
            for (size_t k = 0; k < current.size(); k++) {
                sum += encoder_weights_[layer][j][k] * current[k];
            }
            output[j] = relu(sum);
        }
        current = std::move(output);
    }

    for (int layer = 0; layer < 3; layer++) {
        std::vector<float> output(decoder_dims_[layer], 0.0f);
        for (int j = 0; j < decoder_dims_[layer]; j++) {
            float sum = decoder_bias_[layer][j];
            for (size_t k = 0; k < current.size(); k++) {
                sum += decoder_weights_[layer][j][k] * current[k];
            }
            if (layer < 2) {
                output[j] = relu(sum);
            } else {
                output[j] = sum;
            }
        }
        current = std::move(output);
    }

    return current;
}

void DeepAutoEncoder::backward(const std::vector<float>& input,
                                const std::vector<float>& output,
                                float learning_rate) {
    std::vector<std::vector<float>> activations;
    std::vector<std::vector<float>> pre_activations;

    std::vector<float> current = input;
    activations.push_back(current);

    for (int layer = 0; layer < 3; layer++) {
        std::vector<float> pre(encoder_dims_[layer], 0.0f);
        std::vector<float> act(encoder_dims_[layer], 0.0f);
        for (int j = 0; j < encoder_dims_[layer]; j++) {
            float sum = encoder_bias_[layer][j];
            for (size_t k = 0; k < current.size(); k++) {
                sum += encoder_weights_[layer][j][k] * current[k];
            }
            pre[j] = sum;
            act[j] = relu(sum);
        }
        pre_activations.push_back(pre);
        current = act;
        activations.push_back(current);
    }

    for (int layer = 0; layer < 3; layer++) {
        std::vector<float> pre(decoder_dims_[layer], 0.0f);
        std::vector<float> act(decoder_dims_[layer], 0.0f);
        for (int j = 0; j < decoder_dims_[layer]; j++) {
            float sum = decoder_bias_[layer][j];
            for (size_t k = 0; k < current.size(); k++) {
                sum += decoder_weights_[layer][j][k] * current[k];
            }
            pre[j] = sum;
            if (layer < 2) {
                act[j] = relu(sum);
            } else {
                act[j] = sum;
            }
        }
        pre_activations.push_back(pre);
        current = act;
        activations.push_back(current);
    }

    std::vector<float> delta(output.size());
    for (size_t i = 0; i < output.size(); i++) {
        delta[i] = (output[i] - input[i]) / static_cast<float>(input.size());
    }

    for (int layer = 2; layer >= 0; layer--) {
        int act_idx = 6 + layer;
        int prev_act_idx = act_idx - 1;

        for (int j = 0; j < decoder_dims_[layer]; j++) {
            for (size_t k = 0; k < activations[prev_act_idx].size(); k++) {
                decoder_weights_[layer][j][k] -= learning_rate * delta[j] * activations[prev_act_idx][k];
            }
            decoder_bias_[layer][j] -= learning_rate * delta[j];
        }

        if (layer > 0) {
            std::vector<float> new_delta(activations[prev_act_idx].size(), 0.0f);
            for (size_t k = 0; k < activations[prev_act_idx].size(); k++) {
                float sum = 0.0f;
                for (int j = 0; j < decoder_dims_[layer]; j++) {
                    sum += decoder_weights_[layer][j][k] * delta[j];
                }
                new_delta[k] = sum * reluDerivative(pre_activations[3 + layer - 1][k]);
            }
            delta = std::move(new_delta);
        }
    }

    std::vector<float> enc_delta;
    {
        std::vector<float> new_delta(activations[3].size(), 0.0f);
        for (size_t k = 0; k < activations[3].size(); k++) {
            float sum = 0.0f;
            for (int j = 0; j < decoder_dims_[0]; j++) {
                sum += decoder_weights_[0][j][k] * delta[j];
            }
            new_delta[k] = sum * reluDerivative(pre_activations[2][k]);
        }
        enc_delta = std::move(new_delta);
    }
    delta = enc_delta;

    for (int layer = 2; layer >= 0; layer--) {
        int prev_act_idx = layer;

        for (int j = 0; j < encoder_dims_[layer]; j++) {
            for (size_t k = 0; k < activations[prev_act_idx].size(); k++) {
                encoder_weights_[layer][j][k] -= learning_rate * delta[j] * activations[prev_act_idx][k];
            }
            encoder_bias_[layer][j] -= learning_rate * delta[j];
        }

        if (layer > 0) {
            std::vector<float> new_delta(activations[layer].size(), 0.0f);
            for (size_t k = 0; k < activations[layer].size(); k++) {
                float sum = 0.0f;
                for (int j = 0; j < encoder_dims_[layer]; j++) {
                    sum += encoder_weights_[layer][j][k] * delta[j];
                }
                new_delta[k] = sum * reluDerivative(pre_activations[layer - 1][k]);
            }
            delta = std::move(new_delta);
        }
    }
}

void DeepAutoEncoder::train(const std::vector<std::vector<float>>& data, int epochs) {
    for (int epoch = 0; epoch < epochs; epoch++) {
        for (const auto& sample : data) {
            std::vector<float> output = forward(sample);
            backward(sample, output, learning_rate_);
        }
    }
}

std::vector<float> DeepAutoEncoder::reconstruct(const std::vector<float>& input) {
    return forward(input);
}

float DeepAutoEncoder::anomalyScore(const std::vector<float>& input) {
    std::vector<float> output = forward(input);
    float mse = 0.0f;
    for (size_t i = 0; i < input.size(); i++) {
        float diff = input[i] - output[i];
        mse += diff * diff;
    }
    mse /= static_cast<float>(input.size());
    return 1.0f - std::exp(-mse);
}

CavitationDetector::CavitationDetector(int input_dim, const Thresholds& thresholds)
    : thresholds_(thresholds), input_dim_(input_dim),
      isolation_forest_(100, 256), autoencoder_(input_dim, 16, 0.001f) {}

void CavitationDetector::train(const std::vector<std::vector<float>>& normal_data, int epochs) {
    isolation_forest_.fit(normal_data);
    autoencoder_.train(normal_data, epochs);
}

std::vector<float> CavitationDetector::extractFeatureVector(const SpectrumFeature& feature) {
    std::vector<float> vec;
    vec.push_back(static_cast<float>(feature.band_power_low));
    vec.push_back(static_cast<float>(feature.band_power_mid));
    vec.push_back(static_cast<float>(feature.band_power_high));
    for (float e : feature.wavelet_energy) {
        vec.push_back(e);
    }
    vec.resize(input_dim_, 0.0f);
    return vec;
}

CavitationStatus CavitationDetector::detect(const SpectrumFeature& feature) {
    CavitationStatus status;
    status.turbine_id = feature.turbine_id;
    status.blade_id = 0;
    status.zone_id = 0;
    status.timestamp_ms = feature.timestamp_ms;

    std::vector<float> feat_vec = extractFeatureVector(feature);

    float if_score = isolation_forest_.score(feat_vec);
    float ae_score = autoencoder_.anomalyScore(feat_vec);

    float ensemble_score = 0.6f * if_score + 0.4f * ae_score;

    float high_freq_ratio = 0.0f;
    if (feature.wavelet_energy.size() >= 16) {
        float high_energy = 0.0f;
        float total_energy = 0.0f;
        for (size_t i = 0; i < feature.wavelet_energy.size(); i++) {
            total_energy += feature.wavelet_energy[i];
            if (i >= 8) {
                high_energy += feature.wavelet_energy[i];
            }
        }
        if (total_energy > 0.0f) {
            high_freq_ratio = high_energy / total_energy;
        }
    }

    float intensity = ensemble_score;
    if (high_freq_ratio > 0.5f) {
        intensity = std::min(1.0f, intensity * 1.2f);
    }

    status.anomaly_score = ensemble_score;
    status.intensity = intensity;
    status.detection_method = DetectionMethod::ENSEMBLE;

    if (intensity < thresholds_.incipient_intensity) {
        status.stage = CavitationStage::NONE;
    } else if (intensity < thresholds_.critical_intensity) {
        status.stage = CavitationStage::INCIPIENT;
    } else if (intensity < thresholds_.developed_intensity) {
        status.stage = CavitationStage::CRITICAL;
    } else {
        status.stage = CavitationStage::DEVELOPED;
    }

    return status;
}
