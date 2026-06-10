#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <httplib.h>

#include "types.h"

class ApiServer {
public:
    explicit ApiServer(uint16_t port = 8080);
    ~ApiServer();

    ApiServer(const ApiServer&) = delete;
    ApiServer& operator=(const ApiServer&) = delete;

    bool start();
    void stop();
    bool isRunning() const;
    uint16_t getPort() const;

    using TurbineListCallback = std::function<std::vector<uint8_t>()>;
    using CavitationStatusCallback = std::function<CavitationStatus(uint8_t)>;
    using CavitationHistoryCallback = std::function<std::vector<CavitationStatus>(uint8_t, uint8_t)>;
    using FatigueInfoCallback = std::function<FatigueDamage(uint8_t, uint8_t)>;
    using SpectrumCallback = std::function<SpectrumFeature(uint8_t, uint8_t)>;
    using WaterfallCallback = std::function<std::vector<SpectrumFeature>(uint8_t, uint8_t, int)>;
    using AlarmListCallback = std::function<std::vector<AlarmRecord>(AlarmSeverity)>;
    using ConfigCallback = std::function<std::string()>;
    using RealtimeCallback = std::function<void(uint8_t, std::function<void(const CavitationStatus&)>)>;

    void setTurbineListHandler(TurbineListCallback cb);
    void setCavitationStatusHandler(CavitationStatusCallback cb);
    void setCavitationHistoryHandler(CavitationHistoryCallback cb);
    void setFatigueInfoHandler(FatigueInfoCallback cb);
    void setSpectrumHandler(SpectrumCallback cb);
    void setWaterfallHandler(WaterfallCallback cb);
    void setAlarmListHandler(AlarmListCallback cb);
    void setConfigHandler(ConfigCallback cb);
    void setRealtimeHandler(RealtimeCallback cb);

private:
    void registerRoutes();
    std::string serializeCavitationStatus(const CavitationStatus& status);
    std::string serializeFatigueDamage(const FatigueDamage& damage);
    std::string serializeSpectrumFeature(const SpectrumFeature& feature);
    std::string serializeAlarmRecord(const AlarmRecord& alarm);

    uint16_t port_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;

    TurbineListCallback turbine_list_cb_;
    CavitationStatusCallback cavitation_status_cb_;
    CavitationHistoryCallback cavitation_history_cb_;
    FatigueInfoCallback fatigue_info_cb_;
    SpectrumCallback spectrum_cb_;
    WaterfallCallback waterfall_cb_;
    AlarmListCallback alarm_list_cb_;
    ConfigCallback config_cb_;
    RealtimeCallback realtime_cb_;
};
