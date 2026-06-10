#pragma once

#include <vector>
#include <cstddef>

#include "common.h"

class SignalProcessor {
public:
    SignalProcessor() = default;
    ~SignalProcessor() = default;

    SignalProcessor(const SignalProcessor&) = delete;
    SignalProcessor& operator=(const SignalProcessor&) = delete;

    void computeFFT(const std::vector<float>& input, std::vector<float>& magnitude);

    double computeBandPower(const std::vector<float>& magnitude,
                            float sample_rate,
                            float low_hz,
                            float high_hz);

    float computeDominantFreq(const std::vector<float>& magnitude, float sample_rate);

    void computeWaveletPacketEnergy(const std::vector<float>& signal,
                                    int levels,
                                    std::vector<float>& energies);

    SpectrumFeature computeSpectralFeatures(const SensorData& data);

    void setSampleRate(float rate);
    float getSampleRate() const;

private:
    void daubechies4Decompose(const std::vector<float>& input,
                              std::vector<float>& approx,
                              std::vector<float>& detail);

    float sample_rate_{50000.0f};
};
