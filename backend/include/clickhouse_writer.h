#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include <clickhouse/client.h>

#include "types.h"

class ClickHouseWriter {
public:
    explicit ClickHouseWriter(const std::string& connection_string);
    ~ClickHouseWriter();

    ClickHouseWriter(const ClickHouseWriter&) = delete;
    ClickHouseWriter& operator=(const ClickHouseWriter&) = delete;

    void writeRawSignal(const SensorData& data);
    void writeSpectrumFeature(const SpectrumFeature& feature);
    void writeCavitationStatus(const CavitationStatus& status);
    void writeFatigueDamage(const FatigueDamage& damage);
    void writeAlarmRecord(const AlarmRecord& alarm);

    bool start();
    void stop();
    void flush();
    bool isRunning() const;

private:
    enum class WriteType : uint8_t {
        RAW_SIGNAL = 0,
        SPECTRUM_FEATURE = 1,
        CAVITATION_STATUS = 2,
        FATIGUE_DAMAGE = 3,
        ALARM_RECORD = 4
    };

    struct WriteTask {
        WriteType type;
        std::vector<uint8_t> serialized_data;
    };

    void writerLoop();
    void processTask(const WriteTask& task);
    bool connect();

    std::string connection_string_;
    clickhouse::Client* client_{nullptr};

    std::atomic<bool> running_{false};
    std::thread writer_thread_;

    std::queue<WriteTask> queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    static constexpr size_t MAX_QUEUE_SIZE = 10000;
    static constexpr size_t BATCH_SIZE = 1000;
};
