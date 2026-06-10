#include "signal_processor.h"

#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>

static size_t nextPowerOf2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void SignalProcessor::computeFFT(const std::vector<float>& input, std::vector<float>& magnitude) {
    size_t n = nextPowerOf2(input.size());
    std::vector<std::complex<float>> x(n);
    for (size_t i = 0; i < input.size(); i++) {
        x[i] = std::complex<float>(input[i], 0.0f);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * static_cast<float>(M_PI) / static_cast<float>(len);
        std::complex<float> wn(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; j++) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = w * x[i + j + len / 2];
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wn;
            }
        }
    }

    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(x[i], x[j]);
        }
    }

    size_t half = n / 2;
    magnitude.resize(half);
    for (size_t i = 0; i < half; i++) {
        magnitude[i] = std::abs(x[i]) / static_cast<float>(n);
    }
}

double SignalProcessor::computeBandPower(const std::vector<float>& magnitude,
                                          float sample_rate,
                                          float low_hz,
                                          float high_hz) {
    if (magnitude.empty()) return 0.0;

    size_t n = magnitude.size() * 2;
    float freq_res = sample_rate / static_cast<float>(n);

    size_t low_idx = static_cast<size_t>(low_hz / freq_res);
    size_t high_idx = static_cast<size_t>(high_hz / freq_res);
    low_idx = std::min(low_idx, magnitude.size() - 1);
    high_idx = std::min(high_idx, magnitude.size() - 1);

    double power = 0.0;
    for (size_t i = low_idx; i <= high_idx && i < magnitude.size(); i++) {
        float m = magnitude[i];
        power += static_cast<double>(m) * static_cast<double>(m);
    }
    power *= freq_res;

    return power;
}

float SignalProcessor::computeDominantFreq(const std::vector<float>& magnitude, float sample_rate) {
    if (magnitude.empty()) return 0.0f;

    size_t n = magnitude.size() * 2;
    float freq_res = sample_rate / static_cast<float>(n);

    size_t peak_idx = 0;
    float peak_val = magnitude[0];
    for (size_t i = 1; i < magnitude.size(); i++) {
        if (magnitude[i] > peak_val) {
            peak_val = magnitude[i];
            peak_idx = i;
        }
    }

    return static_cast<float>(peak_idx) * freq_res;
}

void SignalProcessor::daubechies4Decompose(const std::vector<float>& input,
                                            std::vector<float>& approx,
                                            std::vector<float>& detail) {
    static const float h[] = {-0.0106f, 0.0329f, 0.0308f, -0.1870f,
                              -0.0280f, 0.6309f, 0.7148f, 0.2304f};
    static const int filt_len = 8;

    size_t n = input.size();
    size_t out_len = (n + filt_len - 1) / 2;
    approx.resize(out_len);
    detail.resize(out_len);

    for (size_t i = 0; i < out_len; i++) {
        float a = 0.0f;
        float d = 0.0f;
        for (int k = 0; k < filt_len; k++) {
            int idx = static_cast<int>(2 * i + k) - (filt_len - 1);
            if (idx < 0) idx += static_cast<int>(n);
            if (idx >= static_cast<int>(n)) idx -= static_cast<int>(n);
            float val = input[static_cast<size_t>(idx)];
            a += h[k] * val;
            d += h[filt_len - 1 - k] * val * ((k % 2 == 0) ? 1.0f : -1.0f);
        }
        approx[i] = a;
        detail[i] = d;
    }
}

void SignalProcessor::computeWaveletPacketEnergy(const std::vector<float>& signal,
                                                  int levels,
                                                  std::vector<float>& energies) {
    int num_subbands = 1 << levels;
    energies.resize(num_subbands);

    std::vector<std::vector<float>> current_level(1, signal);

    for (int lev = 0; lev < levels; lev++) {
        std::vector<std::vector<float>> next_level;
        for (const auto& band : current_level) {
            if (band.empty()) {
                next_level.push_back({});
                next_level.push_back({});
                continue;
            }
            std::vector<float> approx, detail;
            daubechies4Decompose(band, approx, detail);
            next_level.push_back(std::move(approx));
            next_level.push_back(std::move(detail));
        }
        current_level = std::move(next_level);
    }

    for (int i = 0; i < num_subbands; i++) {
        double energy = 0.0;
        if (i < static_cast<int>(current_level.size())) {
            for (float v : current_level[i]) {
                energy += static_cast<double>(v) * static_cast<double>(v);
            }
        }
        energies[i] = static_cast<float>(energy);
    }
}

SpectrumFeature SignalProcessor::computeSpectralFeatures(const SensorData& data) {
    SpectrumFeature feature;
    feature.turbine_id = data.turbine_id;
    feature.sensor_id = data.sensor_id;
    feature.timestamp_ms = data.timestamp_ms;

    std::vector<float> magnitude;
    computeFFT(data.samples, magnitude);
    feature.fft_magnitude = magnitude;

    feature.dominant_freq = computeDominantFreq(magnitude, sample_rate_);

    feature.band_power_low = computeBandPower(magnitude, sample_rate_, 0.0f, 2000.0f);
    feature.band_power_mid = computeBandPower(magnitude, sample_rate_, 2000.0f, 10000.0f);
    feature.band_power_high = computeBandPower(magnitude, sample_rate_, 10000.0f, 25000.0f);
    feature.total_power = feature.band_power_low + feature.band_power_mid + feature.band_power_high;

    std::vector<float> wavelet_energies;
    computeWaveletPacketEnergy(data.samples, 4, wavelet_energies);
    feature.wavelet_energy = wavelet_energies;

    return feature;
}

void SignalProcessor::setSampleRate(float rate) {
    sample_rate_ = rate;
}

float SignalProcessor::getSampleRate() const {
    return sample_rate_;
}
