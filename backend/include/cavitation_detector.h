#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include <random>
#include <vector>

#include "types.h"

struct IsolationTree {
    int split_feature{-1};
    float split_value{0.0f};
    std::unique_ptr<IsolationTree> left;
    std::unique_ptr<IsolationTree> right;
    bool is_leaf{false};
    int depth{0};
    size_t size{0};
    float anomaly_score{0.0f};
};

class IsolationForest {
public:
    IsolationForest(int n_trees = 100, int sample_size = 256);
    ~IsolationForest() = default;

    IsolationForest(const IsolationForest&) = delete;
    IsolationForest& operator=(const IsolationForest&) = delete;

    void fit(const std::vector<std::vector<float>>& data);
    float score(const std::vector<float>& sample);

private:
    std::unique_ptr<IsolationTree> buildTree(const std::vector<std::vector<float>>& data,
                                              int depth,
                                              int max_depth);
    float pathLength(const IsolationTree* tree,
                     const std::vector<float>& sample,
                     int current_depth);
    float computeAnomalyScore(float path_len);

    int n_trees_;
    int sample_size_;
    int max_depth_;
    std::vector<std::unique_ptr<IsolationTree>> trees_;
    size_t training_size_{0};
    std::mt19937 rng_{std::random_device{}()};
};

struct OperatingCondition {
    float head;
    float flow;
    float rpm;
    float power;
};

class FeatureNormalizer {
public:
    FeatureNormalizer() = default;

    void update(const std::vector<float>& features, const OperatingCondition& cond);
    std::vector<float> normalize(const std::vector<float>& features) const;
    void reset();

    bool hasStats() const { return count_ > 32; }

private:
    static constexpr size_t HISTORY_SIZE = 512;

    struct ConditionBin {
        OperatingCondition cond;
        std::vector<float> mean;
        std::vector<float> stddev;
        size_t count{0};
    };

    std::deque<ConditionBin> bins_;
    int current_bin_{-1};
    size_t count_{0};
    int dim_{0};

    std::vector<float> running_mean_;
    std::vector<float> running_m2_;
    std::vector<float> cached_mean_;
    std::vector<float> cached_std_;

    void updateRunningStats(const std::vector<float>& features);
    int findOrAddBin(const OperatingCondition& cond);
};

class AdaptiveThreshold {
public:
    AdaptiveThreshold(float base_incipient = 0.3f,
                      float base_critical = 0.5f,
                      float base_developed = 0.7f);

    void update(float anomaly_score);
    CavitationStage classify(float intensity) const;

    float getIncipientThreshold() const;
    float getCriticalThreshold() const;
    float getDevelopedThreshold() const;

private:
    static constexpr size_t WINDOW_SIZE = 512;

    std::deque<float> score_window_;
    float base_incipient_;
    float base_critical_;
    float base_developed_;

    float adaptive_offset_{0.0f};

    void recompute();
    float meanScore() const;
    float stdScore() const;
};

class DeepAutoEncoder {
public:
    explicit DeepAutoEncoder(int input_dim,
                             int encoding_dim = 16,
                             float learning_rate = 0.001f);
    ~DeepAutoEncoder() = default;

    DeepAutoEncoder(const DeepAutoEncoder&) = delete;
    DeepAutoEncoder& operator=(const DeepAutoEncoder&) = delete;

    void train(const std::vector<std::vector<float>>& data, int epochs = 50);
    void warmStart(const std::vector<std::vector<float>>& recent_data, int epochs = 5);
    std::vector<float> reconstruct(const std::vector<float>& input);
    float anomalyScore(const std::vector<float>& input);

private:
    std::vector<float> forward(const std::vector<float>& input);
    void backward(const std::vector<float>& input,
                  const std::vector<float>& output,
                  float learning_rate);
    float relu(float x) const;
    float reluDerivative(float x) const;

    int input_dim_;
    int encoding_dim_;
    float learning_rate_;

    std::vector<std::vector<float>> encoder_weights_[3];
    std::vector<float> encoder_bias_[3];
    std::vector<std::vector<float>> decoder_weights_[3];
    std::vector<float> decoder_bias_[3];

    std::vector<int> encoder_dims_;
    std::vector<int> decoder_dims_;

    std::vector<float> baseline_recon_errors_;
    float baseline_mean_{0.0f};
    float baseline_std_{1.0f};
};

class CavitationDetector {
public:
    struct Thresholds {
        float incipient_score{0.4f};
        float critical_score{0.6f};
        float developed_score{0.8f};
        float incipient_intensity{0.3f};
        float critical_intensity{0.5f};
        float developed_intensity{0.7f};
    };

    CavitationDetector(int input_dim, const Thresholds& thresholds = Thresholds{});
    ~CavitationDetector() = default;

    CavitationDetector(const CavitationDetector&) = delete;
    CavitationDetector& operator=(const CavitationDetector&) = delete;

    void train(const std::vector<std::vector<float>>& normal_data, int epochs = 50);
    CavitationStatus detect(const SpectrumFeature& feature,
                            const OperatingCondition& cond = OperatingCondition{});

    void setOperatingCondition(const OperatingCondition& cond);

private:
    std::vector<float> extractFeatureVector(const SpectrumFeature& feature);
    bool detectConditionShift(const OperatingCondition& new_cond);

    Thresholds thresholds_;
    int input_dim_;
    IsolationForest isolation_forest_;
    DeepAutoEncoder autoencoder_;
    FeatureNormalizer normalizer_;
    AdaptiveThreshold adaptive_threshold_;

    OperatingCondition current_cond_;
    bool cond_initialized_{false};
    static constexpr float COND_SHIFT_TOLERANCE = 0.15f;

    std::deque<std::vector<float>> recent_normal_samples_;
    static constexpr size_t WARMSTART_POOL_SIZE = 256;
    static constexpr int WARMSTART_EPOCHS = 5;
    int detect_count_{0};
};
