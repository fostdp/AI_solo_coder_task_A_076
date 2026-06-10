#include "clickhouse_writer.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

ClickHouseWriter::ClickHouseWriter(const std::string& connection_string)
    : connection_string_(connection_string) {}

ClickHouseWriter::~ClickHouseWriter() {
    stop();
}

bool ClickHouseWriter::connect() {
    try {
        clickhouse::ClientOptions opts;
        std::string host = "localhost";
        uint16_t port = 9000;
        std::string user = "default";
        std::string password;
        std::string database = "cavitation_monitor";

        std::istringstream iss(connection_string_);
        std::string token;
        while (std::getline(iss, token, ';')) {
            size_t eq = token.find('=');
            if (eq == std::string::npos) continue;
            std::string key = token.substr(0, eq);
            std::string val = token.substr(eq + 1);
            if (key == "host") host = val;
            else if (key == "port") port = static_cast<uint16_t>(std::stoi(val));
            else if (key == "user") user = val;
            else if (key == "password") password = val;
            else if (key == "database") database = val;
        }

        opts.SetHost(host);
        opts.SetPort(port);
        opts.SetUser(user);
        opts.SetPassword(password);
        opts.SetDefaultDatabase(database);

        client_ = new clickhouse::Client(opts);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "ClickHouse connection failed: " << e.what() << std::endl;
        delete client_;
        client_ = nullptr;
        return false;
    }
}

bool ClickHouseWriter::start() {
    if (!connect()) return false;

    running_ = true;
    writer_thread_ = std::thread(&ClickHouseWriter::writerLoop, this);
    return true;
}

void ClickHouseWriter::stop() {
    if (!running_.exchange(false)) return;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }

    if (writer_thread_.joinable()) {
        writer_thread_.join();
    }

    delete client_;
    client_ = nullptr;
}

void ClickHouseWriter::flush() {
    std::queue<WriteTask> tasks;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        tasks.swap(queue_);
    }

    while (!tasks.empty()) {
        processTask(tasks.front());
        tasks.pop();
    }
}

bool ClickHouseWriter::isRunning() const {
    return running_;
}

void ClickHouseWriter::writerLoop() {
    while (running_ || !queue_.empty()) {
        WriteTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait_for(lock, std::chrono::milliseconds(100),
                               [this] { return !queue_.empty() || !running_; });
            if (queue_.empty()) continue;
            task = std::move(queue_.front());
            queue_.pop();
        }
        processTask(task);
    }
}

void ClickHouseWriter::processTask(const WriteTask& task) {
    if (!client_) return;

    try {
        switch (task.type) {
            case WriteType::RAW_SIGNAL: {
                clickhouse::Block block;
                auto ts_col = std::make_shared<clickhouse::ColumnDateTime64>(3);
                auto turbine_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto sensor_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto sensor_type_col = std::make_shared<clickhouse::ColumnEnum8>();
                auto location_col = std::make_shared<clickhouse::ColumnEnum8>();
                auto sample_rate_col = std::make_shared<clickhouse::ColumnUInt32>();
                auto signal_col = std::make_shared<clickhouse::ColumnArray>(
                    std::make_shared<clickhouse::ColumnFloat32>());
                auto length_col = std::make_shared<clickhouse::ColumnUInt32>();

                SensorData data;
                std::memcpy(&data, task.serialized_data.data(), sizeof(SensorData));
                size_t sample_count = (task.serialized_data.size() - sizeof(SensorData)) / sizeof(float);
                std::vector<float> samples(sample_count);
                if (sample_count > 0) {
                    std::memcpy(samples.data(),
                                task.serialized_data.data() + sizeof(SensorData),
                                sample_count * sizeof(float));
                }

                ts_col->Append(std::chrono::system_clock::now());
                turbine_col->Append(data.turbine_id);
                sensor_col->Append(data.sensor_id);
                sensor_type_col->Append(static_cast<int8_t>(data.sensor_type));
                location_col->Append(static_cast<int8_t>(data.location));
                sample_rate_col->Append(50000);
                auto arr_col = std::make_shared<clickhouse::ColumnFloat32>();
                for (float s : samples) arr_col->Append(s);
                signal_col->AppendAsColumn(arr_col);
                length_col->Append(static_cast<uint32_t>(sample_count));

                block.AppendColumn("timestamp", ts_col);
                block.AppendColumn("turbine_id", turbine_col);
                block.AppendColumn("sensor_id", sensor_col);
                block.AppendColumn("sensor_type", sensor_type_col);
                block.AppendColumn("location", location_col);
                block.AppendColumn("sample_rate", sample_rate_col);
                block.AppendColumn("signal_data", signal_col);
                block.AppendColumn("signal_length", length_col);

                client_->Insert("raw_signal", block);
                break;
            }
            case WriteType::SPECTRUM_FEATURE: {
                SpectrumFeature feat;
                std::memcpy(&feat, task.serialized_data.data(), sizeof(SpectrumFeature));
                size_t fft_len = (task.serialized_data.size() - sizeof(SpectrumFeature)) / sizeof(float);
                std::vector<float> fft_mag(fft_len);
                if (fft_len > 0) {
                    std::memcpy(fft_mag.data(),
                                task.serialized_data.data() + sizeof(SpectrumFeature),
                                fft_len * sizeof(float));
                }

                clickhouse::Block block;
                auto ts_col = std::make_shared<clickhouse::ColumnDateTime64>(3);
                auto turbine_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto sensor_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto fft_col = std::make_shared<clickhouse::ColumnArray>(
                    std::make_shared<clickhouse::ColumnFloat32>());
                auto fft_len_col = std::make_shared<clickhouse::ColumnUInt32>();
                auto dom_freq_col = std::make_shared<clickhouse::ColumnFloat32>();
                auto total_pwr_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto bp_low_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto bp_mid_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto bp_high_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto wl_col = std::make_shared<clickhouse::ColumnArray>(
                    std::make_shared<clickhouse::ColumnFloat32>());
                auto wl_lvl_col = std::make_shared<clickhouse::ColumnUInt8>();

                ts_col->Append(std::chrono::system_clock::now());
                turbine_col->Append(feat.turbine_id);
                sensor_col->Append(feat.sensor_id);
                auto arr_fft = std::make_shared<clickhouse::ColumnFloat32>();
                for (float v : fft_mag) arr_fft->Append(v);
                fft_col->AppendAsColumn(arr_fft);
                fft_len_col->Append(static_cast<uint32_t>(fft_len));
                dom_freq_col->Append(feat.dominant_freq);
                total_pwr_col->Append(feat.total_power);
                bp_low_col->Append(feat.band_power_low);
                bp_mid_col->Append(feat.band_power_mid);
                bp_high_col->Append(feat.band_power_high);
                auto arr_wl = std::make_shared<clickhouse::ColumnFloat32>();
                for (float v : feat.wavelet_energy) arr_wl->Append(v);
                wl_col->AppendAsColumn(arr_wl);
                wl_lvl_col->Append(4);

                block.AppendColumn("timestamp", ts_col);
                block.AppendColumn("turbine_id", turbine_col);
                block.AppendColumn("sensor_id", sensor_col);
                block.AppendColumn("fft_magnitude", fft_col);
                block.AppendColumn("fft_length", fft_len_col);
                block.AppendColumn("dominant_freq", dom_freq_col);
                block.AppendColumn("total_power", total_pwr_col);
                block.AppendColumn("band_power_low", bp_low_col);
                block.AppendColumn("band_power_mid", bp_mid_col);
                block.AppendColumn("band_power_high", bp_high_col);
                block.AppendColumn("wavelet_energy", wl_col);
                block.AppendColumn("wavelet_levels", wl_lvl_col);

                client_->Insert("spectrum_feature", block);
                break;
            }
            case WriteType::CAVITATION_STATUS: {
                CavitationStatus status;
                std::memcpy(&status, task.serialized_data.data(), sizeof(CavitationStatus));

                clickhouse::Block block;
                auto ts_col = std::make_shared<clickhouse::ColumnDateTime64>(3);
                auto turbine_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto blade_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto zone_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto stage_col = std::make_shared<clickhouse::ColumnEnum8>();
                auto intensity_col = std::make_shared<clickhouse::ColumnFloat32>();
                auto score_col = std::make_shared<clickhouse::ColumnFloat32>();
                auto method_col = std::make_shared<clickhouse::ColumnEnum8>();

                ts_col->Append(std::chrono::system_clock::now());
                turbine_col->Append(status.turbine_id);
                blade_col->Append(status.blade_id);
                zone_col->Append(status.zone_id);
                stage_col->Append(static_cast<int8_t>(status.stage));
                intensity_col->Append(status.intensity);
                score_col->Append(status.anomaly_score);
                method_col->Append(static_cast<int8_t>(status.detection_method));

                block.AppendColumn("timestamp", ts_col);
                block.AppendColumn("turbine_id", turbine_col);
                block.AppendColumn("blade_id", blade_col);
                block.AppendColumn("zone_id", zone_col);
                block.AppendColumn("cavitation_stage", stage_col);
                block.AppendColumn("cavitation_intensity", intensity_col);
                block.AppendColumn("anomaly_score", score_col);
                block.AppendColumn("detection_method", method_col);

                client_->Insert("cavitation_status", block);
                break;
            }
            case WriteType::FATIGUE_DAMAGE: {
                FatigueDamage dmg;
                std::memcpy(&dmg, task.serialized_data.data(), sizeof(FatigueDamage));

                clickhouse::Block block;
                auto ts_col = std::make_shared<clickhouse::ColumnDateTime64>(3);
                auto turbine_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto blade_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto zone_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto stress_amp_col = std::make_shared<clickhouse::ColumnFloat32>();
                auto stress_mean_col = std::make_shared<clickhouse::ColumnFloat32>();
                auto cycle_col = std::make_shared<clickhouse::ColumnUInt64>();
                auto miner_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto cum_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto remain_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto sn_col = std::make_shared<clickhouse::ColumnString>();

                ts_col->Append(std::chrono::system_clock::now());
                turbine_col->Append(dmg.turbine_id);
                blade_col->Append(dmg.blade_id);
                zone_col->Append(dmg.zone_id);
                stress_amp_col->Append(dmg.stress_amplitude);
                stress_mean_col->Append(dmg.stress_mean);
                cycle_col->Append(dmg.cycle_count);
                miner_col->Append(dmg.miner_damage);
                cum_col->Append(dmg.cumulative_damage);
                remain_col->Append(dmg.remaining_life_hours);
                sn_col->Append("S-N: 620/480/380/300/240/200/180/170 MPa");

                block.AppendColumn("timestamp", ts_col);
                block.AppendColumn("turbine_id", turbine_col);
                block.AppendColumn("blade_id", blade_col);
                block.AppendColumn("zone_id", zone_col);
                block.AppendColumn("stress_amplitude", stress_amp_col);
                block.AppendColumn("stress_mean", stress_mean_col);
                block.AppendColumn("cycle_count", cycle_col);
                block.AppendColumn("miner_damage", miner_col);
                block.AppendColumn("cumulative_damage", cum_col);
                block.AppendColumn("remaining_life_hours", remain_col);
                block.AppendColumn("sn_params", sn_col);

                client_->Insert("fatigue_damage", block);
                break;
            }
            case WriteType::ALARM_RECORD: {
                AlarmRecord alarm;
                std::memcpy(&alarm, task.serialized_data.data(), sizeof(AlarmRecord));
                size_t str_offset = sizeof(AlarmRecord);
                size_t str_len = task.serialized_data.size() - str_offset;
                std::string advice(reinterpret_cast<const char*>(task.serialized_data.data() + str_offset),
                                   str_len);

                clickhouse::Block block;
                auto ts_col = std::make_shared<clickhouse::ColumnDateTime64>(3);
                auto turbine_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto type_col = std::make_shared<clickhouse::ColumnEnum8>();
                auto sev_col = std::make_shared<clickhouse::ColumnEnum8>();
                auto blade_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto zone_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto measured_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto threshold_col = std::make_shared<clickhouse::ColumnFloat64>();
                auto iec_col = std::make_shared<clickhouse::ColumnUInt8>();
                auto advice_col = std::make_shared<clickhouse::ColumnString>();

                ts_col->Append(std::chrono::system_clock::now());
                turbine_col->Append(alarm.turbine_id);
                type_col->Append(static_cast<int8_t>(alarm.alarm_type));
                sev_col->Append(static_cast<int8_t>(alarm.severity));
                blade_col->Append(alarm.blade_id);
                zone_col->Append(alarm.zone_id);
                measured_col->Append(alarm.measured_value);
                threshold_col->Append(alarm.threshold_value);
                iec_col->Append(alarm.iec61850_sent ? 1 : 0);
                advice_col->Append(advice);

                block.AppendColumn("timestamp", ts_col);
                block.AppendColumn("turbine_id", turbine_col);
                block.AppendColumn("alarm_type", type_col);
                block.AppendColumn("severity", sev_col);
                block.AppendColumn("blade_id", blade_col);
                block.AppendColumn("zone_id", zone_col);
                block.AppendColumn("measured_value", measured_col);
                block.AppendColumn("threshold_value", threshold_col);
                block.AppendColumn("iec61850_sent", iec_col);
                block.AppendColumn("maintenance_advice", advice_col);

                client_->Insert("alarm_record", block);
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "ClickHouse write error: " << e.what() << std::endl;
    }
}

void ClickHouseWriter::writeRawSignal(const SensorData& data) {
    WriteTask task;
    task.type = WriteType::RAW_SIGNAL;
    size_t data_size = sizeof(SensorData) + data.samples.size() * sizeof(float);
    task.serialized_data.resize(data_size);
    std::memcpy(task.serialized_data.data(), &data, sizeof(SensorData));
    if (!data.samples.empty()) {
        std::memcpy(task.serialized_data.data() + sizeof(SensorData),
                    data.samples.data(), data.samples.size() * sizeof(float));
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void ClickHouseWriter::writeSpectrumFeature(const SpectrumFeature& feature) {
    WriteTask task;
    task.type = WriteType::SPECTRUM_FEATURE;
    size_t data_size = sizeof(SpectrumFeature) + feature.fft_magnitude.size() * sizeof(float);
    task.serialized_data.resize(data_size);
    std::memcpy(task.serialized_data.data(), &feature, sizeof(SpectrumFeature));
    if (!feature.fft_magnitude.empty()) {
        std::memcpy(task.serialized_data.data() + sizeof(SpectrumFeature),
                    feature.fft_magnitude.data(), feature.fft_magnitude.size() * sizeof(float));
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void ClickHouseWriter::writeCavitationStatus(const CavitationStatus& status) {
    WriteTask task;
    task.type = WriteType::CAVITATION_STATUS;
    task.serialized_data.resize(sizeof(CavitationStatus));
    std::memcpy(task.serialized_data.data(), &status, sizeof(CavitationStatus));

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void ClickHouseWriter::writeFatigueDamage(const FatigueDamage& damage) {
    WriteTask task;
    task.type = WriteType::FATIGUE_DAMAGE;
    task.serialized_data.resize(sizeof(FatigueDamage));
    std::memcpy(task.serialized_data.data(), &damage, sizeof(FatigueDamage));

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void ClickHouseWriter::writeAlarmRecord(const AlarmRecord& alarm) {
    WriteTask task;
    task.type = WriteType::ALARM_RECORD;
    size_t data_size = sizeof(AlarmRecord) + alarm.maintenance_advice.size();
    task.serialized_data.resize(data_size);
    std::memcpy(task.serialized_data.data(), &alarm, sizeof(AlarmRecord));
    if (!alarm.maintenance_advice.empty()) {
        std::memcpy(task.serialized_data.data() + sizeof(AlarmRecord),
                    alarm.maintenance_advice.data(), alarm.maintenance_advice.size());
    }

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= MAX_QUEUE_SIZE) {
            queue_.pop();
        }
        queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}
