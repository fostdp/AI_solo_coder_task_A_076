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

void FeatureNormalizer::update(const std::vector<float>& features, const OperatingCondition& cond) {
    if (dim_ == 0) {
        dim_ = static_cast<int>(features.size());
        running_mean_.resize(dim_, 0.0f);
        running_m2_.resize(dim_, 0.0f);
        cached_mean_.resize(dim_, 0.0f);
        cached_std_.resize(dim_, 1.0f);
    }

    int bin_idx = findOrAddBin(cond);
    ConditionBin& bin = bins_[bin_idx];
    bin.count++;

    if (bin.mean.empty()) {
        bin.mean = features;
        bin.stddev.resize(dim_, 1.0f);
    } else {
        float alpha = 1.0f / static_cast<float>(bin.count);
        for (int i = 0; i < dim_; i++) {
            bin.mean[i] += alpha * (features[i] - bin.mean[i]);
            float diff = features[i] - bin.mean[i];
            float old_std = bin.stddev[i];
            bin.stddev[i] = std::max(0.001f,
                std::sqrt(old_std * old_std * (1.0f - alpha) + diff * diff * alpha));
        }
    }

    cached_mean_ = bin.mean;
    cached_std_ = bin.stddev;
    count_++;
}

std::vector<float> FeatureNormalizer::normalize(const std::vector<float>& features) const {
    std::vector<float> result(features.size());
    for (size_t i = 0; i < features.size(); i++) {
        result[i] = (features[i] - cached_mean_[i]) / cached_std_[i];
        result[i] = std::max(-5.0f, std::min(5.0f, result[i]));
    }
    return result;
}

void FeatureNormalizer::reset() {
    bins_.clear();
    current_bin_ = -1;
    count_ = 0;
}

int FeatureNormalizer::findOrAddBin(const OperatingCondition& cond) {
    static constexpr float HEAD_TOL = 5.0f;
    static constexpr float POWER_TOL = 20.0f;

    for (int i = 0; i < static_cast<int>(bins_.size()); i++) {
        float head_diff = std::abs(bins_[i].cond.head - cond.head);
        float power_diff = std::abs(bins_[i].cond.power - cond.power);
        if (head_diff < HEAD_TOL && power_diff < POWER_TOL) {
            current_bin_ = i;
            return i;
        }
    }

    ConditionBin new_bin;
    new_bin.cond = cond;
    bins_.push_back(std::move(new_bin));

    if (bins_.size() > HISTORY_SIZE) {
        bins_.pop_front();
    }

    current_bin_ = static_cast<int>(bins_.size()) - 1;
    return current_bin_;
}

AdaptiveThreshold::AdaptiveThreshold(float base_incipient, float base_critical, float base_developed)
    : base_incipient_(base_incipient), base_critical_(base_critical),
      base_developed_(base_developed) {}

void AdaptiveThreshold::update(float anomaly_score) {
    score_window_.push_back(anomaly_score);
    if (score_window_.size() > WINDOW_SIZE) {
        score_window_.pop_front();
    }
    recompute();
}

CavitationStage AdaptiveThreshold::classify(float intensity) const {
    float incip = base_incipient_ + adaptive_offset_;
    float crit = base_critical_ + adaptive_offset_;
    float devel = base_developed_ + adaptive_offset_;

    if (intensity < incip) return CavitationStage::NONE;
    if (intensity < crit) return CavitationStage::INCIPIENT;
    if (intensity < devel) return CavitationStage::CRITICAL;
    return CavitationStage::DEVELOPED;
}

float AdaptiveThreshold::getIncipientThreshold() const {
    return base_incipient_ + adaptive_offset_;
}

float AdaptiveThreshold::getCriticalThreshold() const {
    return base_critical_ + adaptive_offset_;
}

float AdaptiveThreshold::getDevelopedThreshold() const {
    return base_developed_ + adaptive_offset_;
}

void AdaptiveThreshold::recompute() {
    if (score_window_.size() < 32) {
        adaptive_offset_ = 0.0f;
        return;
    }

    float mu = meanScore();
    float sigma = stdScore();

    float shift = mu + 3.0f * sigma - base_incipient_;
    if (shift > 0.0f) {
        adaptive_offset_ = std::min(0.15f, shift * 0.5f);
    } else {
        adaptive_offset_ = 0.0f;
    }
}

float AdaptiveThreshold::meanScore() const {
    if (score_window_.empty()) return 0.0f;
    float sum = 0.0f;
    for (float s : score_window_) sum += s;
    return sum / static_cast<float>(score_window_.size());
}

float AdaptiveThreshold::stdScore() const {
    if (score_window_.size() < 2) return 1.0f;
    float mu = meanScore();
    float sum2 = 0.0f;
    for (float s : score_window_) {
        float d = s - mu;
        sum2 += d * d;
    }
    return std::sqrt(sum2 / static_cast<float>(score_window_.size() - 1));
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
    baseline_recon_errors_.clear();

    for (int epoch = 0; epoch < epochs; epoch++) {
        for (const auto& sample : data) {
            std::vector<float> output = forward(sample);
            backward(sample, output, learning_rate_);
        }
    }

    for (const auto& sample : data) {
        std::vector<float> output = forward(sample);
        float mse = 0.0f;
        for (size_t i = 0; i < sample.size(); i++) {
            float diff = sample[i] - output[i];
            mse += diff * diff;
        }
        mse /= static_cast<float>(sample.size());
        baseline_recon_errors_.push_back(mse);
    }

    if (!baseline_recon_errors_.empty()) {
        float sum = 0.0f;
        for (float e : baseline_recon_errors_) sum += e;
        baseline_mean_ = sum / static_cast<float>(baseline_recon_errors_.size());
        float var = 0.0f;
        for (float e : baseline_recon_errors_) {
            float d = e - baseline_mean_;
            var += d * d;
        }
        baseline_std_ = std::sqrt(var / static_cast<float>(baseline_recon_errors_.size()));
        if (baseline_std_ < 1e-6f) baseline_std_ = 1e-6f;
    }
}

void DeepAutoEncoder::warmStart(const std::vector<std::vector<float>>& recent_data, int epochs) {
    if (recent_data.empty()) return;

    float warmup_lr = learning_rate_ * 0.1f;

    for (int epoch = 0; epoch < epochs; epoch++) {
        for (const auto& sample : recent_data) {
            std::vector<float> output = forward(sample);
            backward(sample, output, warmup_lr);
        }
    }

    baseline_recon_errors_.clear();
    for (const auto& sample : recent_data) {
        std::vector<float> output = forward(sample);
        float mse = 0.0f;
        for (size_t i = 0; i < sample.size(); i++) {
            float diff = sample[i] - output[i];
            mse += diff * diff;
        }
        mse /= static_cast<float>(sample.size());
        baseline_recon_errors_.push_back(mse);
    }

    float sum = 0.0f;
    for (float e : baseline_recon_errors_) sum += e;
    baseline_mean_ = sum / static_cast<float>(baseline_recon_errors_.size());
    float var = 0.0f;
    for (float e : baseline_recon_errors_) {
        float d = e - baseline_mean_;
        var += d * d;
    }
    baseline_std_ = std::sqrt(var / static_cast<float>(baseline_recon_errors_.size()));
    if (baseline_std_ < 1e-6f) baseline_std_ = 1e-6f;
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

    if (baseline_std_ > 1e-6f) {
        float normalized_mse = (mse - baseline_mean_) / baseline_std_;
        return 1.0f - std::exp(-std::max(0.0f, normalized_mse) * 0.5f);
    }

    return 1.0f - std::exp(-mse);
}

CavitationDetector::CavitationDetector(int input_dim, const Thresholds& thresholds)
    : thresholds_(thresholds), input_dim_(input_dim),
      isolation_forest_(100, 256), autoencoder_(input_dim, 16, 0.001f),
      adaptive_threshold_(thresholds.incipient_intensity,
                          thresholds.critical_intensity,
                          thresholds.developed_intensity) {}

void CavitationDetector::train(const std::vector<std::vector<float>>& normal_data, int epochs) {
    isolation_forest_.fit(normal_data);
    autoencoder_.train(normal_data, epochs);

    for (const auto& sample : normal_data) {
        if (recent_normal_samples_.size() >= WARMSTART_POOL_SIZE) {
            recent_normal_samples_.pop_front();
        }
        recent_normal_samples_.push_back(sample);
    }
}

void CavitationDetector::setOperatingCondition(const OperatingCondition& cond) {
    if (!cond_initialized_) {
        current_cond_ = cond;
        cond_initialized_ = true;
        return;
    }

    if (detectConditionShift(cond)) {
        current_cond_ = cond;

        if (recent_normal_samples_.size() >= 32) {
            std::vector<std::vector<float>> warmup_data(
                recent_normal_samples_.begin(), recent_normal_samples_.end());
            autoencoder_.warmStart(warmup_data, WARMSTART_EPOCHS);
        }
    } else {
        current_cond_.head = current_cond_.head * 0.95f + cond.head * 0.05f;
        current_cond_.flow = current_cond_.flow * 0.95f + cond.flow * 0.05f;
        current_cond_.power = current_cond_.power * 0.95f + cond.power * 0.05f;
        current_cond_.rpm = current_cond_.rpm * 0.95f + cond.rpm * 0.05f;
    }
}

bool CavitationDetector::detectConditionShift(const OperatingCondition& new_cond) {
    if (!cond_initialized_) return false;

    float head_range = std::max(std::abs(current_cond_.head), 1.0f);
    float power_range = std::max(std::abs(current_cond_.power), 1.0f);

    float head_shift = std::abs(new_cond.head - current_cond_.head) / head_range;
    float power_shift = std::abs(new_cond.power - current_cond_.power) / power_range;

    return head_shift > COND_SHIFT_TOLERANCE || power_shift > COND_SHIFT_TOLERANCE;
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

CavitationStatus CavitationDetector::detect(const SpectrumFeature& feature,
                                             const OperatingCondition& cond) {
    CavitationStatus status;
    status.turbine_id = feature.turbine_id;
    status.blade_id = 0;
    status.zone_id = 0;
    status.timestamp_ms = feature.timestamp_ms;

    setOperatingCondition(cond);

    std::vector<float> feat_vec = extractFeatureVector(feature);

    normalizer_.update(feat_vec, cond);
    std::vector<float> normalized = normalizer_.hasStats()
        ? normalizer_.normalize(feat_vec)
        : feat_vec;

    float if_score = isolation_forest_.score(normalized);
    float ae_score = autoencoder_.anomalyScore(normalized);

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

    adaptive_threshold_.update(intensity);

    status.anomaly_score = ensemble_score;
    status.intensity = intensity;
    status.detection_method = DetectionMethod::ENSEMBLE;

    status.stage = adaptive_threshold_.classify(intensity);

    detect_count_++;
    if (status.stage == CavitationStage::NONE && detect_count_ % 10 == 0) {
        if (recent_normal_samples_.size() >= WARMSTART_POOL_SIZE) {
            recent_normal_samples_.pop_front();
        }
        recent_normal_samples_.push_back(normalized);
    }

    return status;
}
