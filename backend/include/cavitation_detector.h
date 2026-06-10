#pragma once

#include <cstddef>
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

class DeepAutoEncoder {
public:
    explicit DeepAutoEncoder(int input_dim,
                             int encoding_dim = 16,
                             float learning_rate = 0.001f);
    ~DeepAutoEncoder() = default;

    DeepAutoEncoder(const DeepAutoEncoder&) = delete;
    DeepAutoEncoder& operator=(const DeepAutoEncoder&) = delete;

    void train(const std::vector<std::vector<float>>& data, int epochs = 50);
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
    CavitationStatus detect(const SpectrumFeature& feature);

private:
    std::vector<float> extractFeatureVector(const SpectrumFeature& feature);

    Thresholds thresholds_;
    int input_dim_;
    IsolationForest isolation_forest_;
    DeepAutoEncoder autoencoder_;
};
