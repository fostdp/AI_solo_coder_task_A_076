#include "api_server.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;

ApiServer::ApiServer(uint16_t port)
    : port_(port), server_(std::make_unique<httplib::Server>()) {}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::registerRoutes() {
    server_->set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    server_->Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    server_->Get("/api/turbines", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        if (turbine_list_cb_) {
            auto ids = turbine_list_cb_();
            json arr = json::array();
            for (auto id : ids) {
                arr.push_back({{"turbine_id", static_cast<int>(id)}});
            }
            res.set_content(arr.dump(), "application/json");
        } else {
            res.set_content("[]", "application/json");
        }
    });

    server_->Get(R"(/api/cavitation/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        int tid = std::stoi(req.matches[1]);
        if (cavitation_status_cb_) {
            CavitationStatus status = cavitation_status_cb_(static_cast<uint8_t>(tid));
            res.set_content(serializeCavitationStatus(status), "application/json");
        } else {
            res.set_content("{}", "application/json");
        }
    });

    server_->Get(R"(/api/cavitation/(\d+)/history/(\d+))",
        [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        int tid = std::stoi(req.matches[1]);
        int limit = std::stoi(req.matches[2]);
        if (cavitation_history_cb_) {
            auto history = cavitation_history_cb_(static_cast<uint8_t>(tid), static_cast<uint8_t>(limit));
            json arr = json::array();
            for (const auto& s : history) {
                arr.push_back(json::parse(serializeCavitationStatus(s)));
            }
            res.set_content(arr.dump(), "application/json");
        } else {
            res.set_content("[]", "application/json");
        }
    });

    server_->Get(R"(/api/fatigue/(\d+)/blade/(\d+))",
        [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        int tid = std::stoi(req.matches[1]);
        int bid = std::stoi(req.matches[2]);
        if (fatigue_info_cb_) {
            FatigueDamage dmg = fatigue_info_cb_(static_cast<uint8_t>(tid), static_cast<uint8_t>(bid));
            res.set_content(serializeFatigueDamage(dmg), "application/json");
        } else {
            res.set_content("{}", "application/json");
        }
    });

    server_->Get(R"(/api/spectrum/(\d+)/sensor/(\d+))",
        [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        int tid = std::stoi(req.matches[1]);
        int sid = std::stoi(req.matches[2]);
        if (spectrum_cb_) {
            SpectrumFeature feat = spectrum_cb_(static_cast<uint8_t>(tid), static_cast<uint8_t>(sid));
            res.set_content(serializeSpectrumFeature(feat), "application/json");
        } else {
            res.set_content("{}", "application/json");
        }
    });

    server_->Get(R"(/api/waterfall/(\d+)/sensor/(\d+)/depth/(\d+))",
        [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        int tid = std::stoi(req.matches[1]);
        int sid = std::stoi(req.matches[2]);
        int depth = std::stoi(req.matches[3]);
        if (waterfall_cb_) {
            auto features = waterfall_cb_(static_cast<uint8_t>(tid), static_cast<uint8_t>(sid), depth);
            json arr = json::array();
            for (const auto& f : features) {
                arr.push_back(json::parse(serializeSpectrumFeature(f)));
            }
            res.set_content(arr.dump(), "application/json");
        } else {
            res.set_content("[]", "application/json");
        }
    });

    server_->Get(R"(/api/alarms)", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        AlarmSeverity severity = AlarmSeverity::INFO;
        if (req.has_param("severity")) {
            int sev = std::stoi(req.get_param_value("severity"));
            severity = static_cast<AlarmSeverity>(sev);
        }
        if (alarm_list_cb_) {
            auto alarms = alarm_list_cb_(severity);
            json arr = json::array();
            for (const auto& a : alarms) {
                arr.push_back(json::parse(serializeAlarmRecord(a)));
            }
            res.set_content(arr.dump(), "application/json");
        } else {
            res.set_content("[]", "application/json");
        }
    });

    server_->Get("/api/config", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        if (config_cb_) {
            std::string config_str = config_cb_();
            res.set_content(config_str, "application/json");
        } else {
            res.set_content("{}", "application/json");
        }
    });

    server_->Get("/api/realtime", [this](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "text/event-stream");
        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");

        res.set_chunked_content_provider(
            "text/event-stream",
            [this](size_t, httplib::DataSink& sink) -> bool {
                if (!realtime_cb_) {
                    return false;
                }

                for (uint8_t tid = 1; tid <= 6; tid++) {
                    realtime_cb_(tid, [&sink](const CavitationStatus& status) {
                        std::string data = serializeCavitationStatus(status);
                        std::string sse_msg = "data: " + data + "\n\n";
                        sink.write(sse_msg.c_str(), sse_msg.size());
                    });
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                return true;
            });
    });
}

std::string ApiServer::serializeCavitationStatus(const CavitationStatus& status) {
    json j;
    j["turbine_id"] = static_cast<int>(status.turbine_id);
    j["blade_id"] = static_cast<int>(status.blade_id);
    j["zone_id"] = static_cast<int>(status.zone_id);
    j["timestamp_ms"] = status.timestamp_ms;
    j["stage"] = static_cast<int>(status.stage);
    j["intensity"] = status.intensity;
    j["anomaly_score"] = status.anomaly_score;
    j["detection_method"] = static_cast<int>(status.detection_method);
    return j.dump();
}

std::string ApiServer::serializeFatigueDamage(const FatigueDamage& damage) {
    json j;
    j["turbine_id"] = static_cast<int>(damage.turbine_id);
    j["blade_id"] = static_cast<int>(damage.blade_id);
    j["zone_id"] = static_cast<int>(damage.zone_id);
    j["timestamp_ms"] = damage.timestamp_ms;
    j["stress_amplitude"] = damage.stress_amplitude;
    j["stress_mean"] = damage.stress_mean;
    j["cycle_count"] = damage.cycle_count;
    j["miner_damage"] = damage.miner_damage;
    j["cumulative_damage"] = damage.cumulative_damage;
    j["remaining_life_hours"] = damage.remaining_life_hours;
    return j.dump();
}

std::string ApiServer::serializeSpectrumFeature(const SpectrumFeature& feature) {
    json j;
    j["turbine_id"] = static_cast<int>(feature.turbine_id);
    j["sensor_id"] = static_cast<int>(feature.sensor_id);
    j["timestamp_ms"] = feature.timestamp_ms;
    j["fft_magnitude"] = feature.fft_magnitude;
    j["dominant_freq"] = feature.dominant_freq;
    j["total_power"] = feature.total_power;
    j["band_power_low"] = feature.band_power_low;
    j["band_power_mid"] = feature.band_power_mid;
    j["band_power_high"] = feature.band_power_high;
    j["wavelet_energy"] = feature.wavelet_energy;
    return j.dump();
}

std::string ApiServer::serializeAlarmRecord(const AlarmRecord& alarm) {
    json j;
    j["turbine_id"] = static_cast<int>(alarm.turbine_id);
    j["alarm_type"] = static_cast<int>(alarm.alarm_type);
    j["severity"] = static_cast<int>(alarm.severity);
    j["blade_id"] = static_cast<int>(alarm.blade_id);
    j["zone_id"] = static_cast<int>(alarm.zone_id);
    j["timestamp_ms"] = alarm.timestamp_ms;
    j["measured_value"] = alarm.measured_value;
    j["threshold_value"] = alarm.threshold_value;
    j["iec61850_sent"] = alarm.iec61850_sent;
    j["maintenance_advice"] = alarm.maintenance_advice;
    return j.dump();
}

bool ApiServer::start() {
    registerRoutes();

    if (!server_->bind_to_port("0.0.0.0", port_)) {
        std::cerr << "Failed to bind API server to port " << port_ << std::endl;
        return false;
    }

    server_thread_ = std::thread([this]() {
        server_->listen_after_bind();
    });

    return true;
}

void ApiServer::stop() {
    if (server_) {
        server_->stop();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

bool ApiServer::isRunning() const {
    return server_ && server_->is_running();
}

uint16_t ApiServer::getPort() const {
    return port_;
}

void ApiServer::setTurbineListHandler(TurbineListCallback cb) {
    turbine_list_cb_ = std::move(cb);
}

void ApiServer::setCavitationStatusHandler(CavitationStatusCallback cb) {
    cavitation_status_cb_ = std::move(cb);
}

void ApiServer::setCavitationHistoryHandler(CavitationHistoryCallback cb) {
    cavitation_history_cb_ = std::move(cb);
}

void ApiServer::setFatigueInfoHandler(FatigueInfoCallback cb) {
    fatigue_info_cb_ = std::move(cb);
}

void ApiServer::setSpectrumHandler(SpectrumCallback cb) {
    spectrum_cb_ = std::move(cb);
}

void ApiServer::setWaterfallHandler(WaterfallCallback cb) {
    waterfall_cb_ = std::move(cb);
}

void ApiServer::setAlarmListHandler(AlarmListCallback cb) {
    alarm_list_cb_ = std::move(cb);
}

void ApiServer::setConfigHandler(ConfigCallback cb) {
    config_cb_ = std::move(cb);
}

void ApiServer::setRealtimeHandler(RealtimeCallback cb) {
    realtime_cb_ = std::move(cb);
}
