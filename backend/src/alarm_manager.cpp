#include "alarm_manager.h"

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

AlarmManager::AlarmManager(const AlarmThreshold& thresholds)
    : thresholds_(thresholds) {}

std::vector<AlarmRecord> AlarmManager::checkAlarms(const CavitationStatus& cavitation,
                                                     const FatigueDamage& fatigue,
                                                     float vibration_velocity) {
    std::vector<AlarmRecord> alarms;

    auto now = std::chrono::system_clock::now();
    uint64_t ts_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());

    if (cavitation.intensity > thresholds_.cavitation_intensity_limit) {
        AlarmRecord alarm;
        alarm.turbine_id = cavitation.turbine_id;
        alarm.alarm_type = AlarmType::CAVITATION_OVERLIMIT;
        alarm.blade_id = cavitation.blade_id;
        alarm.zone_id = cavitation.zone_id;
        alarm.timestamp_ms = ts_ms;
        alarm.measured_value = static_cast<double>(cavitation.intensity);
        alarm.threshold_value = thresholds_.cavitation_intensity_limit;
        alarm.iec61850_sent = false;

        if (cavitation.intensity > thresholds_.cavitation_developed_limit) {
            alarm.severity = AlarmSeverity::CRITICAL;
        } else if (cavitation.intensity > thresholds_.cavitation_intensity_limit) {
            alarm.severity = AlarmSeverity::WARNING;
        } else {
            alarm.severity = AlarmSeverity::INFO;
        }

        alarm.maintenance_advice = generateMaintenanceAdvice(alarm);
        alarm.iec61850_sent = sendIEC61850(alarm);
        alarms.push_back(alarm);
    }

    if (vibration_velocity > thresholds_.vibration_velocity_limit) {
        AlarmRecord alarm;
        alarm.turbine_id = cavitation.turbine_id;
        alarm.alarm_type = AlarmType::VIBRATION_OVERLIMIT;
        alarm.blade_id = cavitation.blade_id;
        alarm.zone_id = cavitation.zone_id;
        alarm.timestamp_ms = ts_ms;
        alarm.measured_value = static_cast<double>(vibration_velocity);
        alarm.threshold_value = thresholds_.vibration_velocity_limit;
        alarm.iec61850_sent = false;

        if (vibration_velocity > thresholds_.vibration_velocity_limit * 1.5) {
            alarm.severity = AlarmSeverity::CRITICAL;
        } else {
            alarm.severity = AlarmSeverity::WARNING;
        }

        alarm.maintenance_advice = generateMaintenanceAdvice(alarm);
        alarm.iec61850_sent = sendIEC61850(alarm);
        alarms.push_back(alarm);
    }

    if (fatigue.cumulative_damage > thresholds_.fatigue_damage_limit) {
        AlarmRecord alarm;
        alarm.turbine_id = fatigue.turbine_id;
        alarm.alarm_type = AlarmType::FATIGUE_WARNING;
        alarm.blade_id = fatigue.blade_id;
        alarm.zone_id = fatigue.zone_id;
        alarm.timestamp_ms = ts_ms;
        alarm.measured_value = fatigue.cumulative_damage;
        alarm.threshold_value = thresholds_.fatigue_damage_limit;
        alarm.iec61850_sent = false;

        if (fatigue.cumulative_damage > 0.95) {
            alarm.severity = AlarmSeverity::CRITICAL;
        } else {
            alarm.severity = AlarmSeverity::WARNING;
        }

        alarm.maintenance_advice = generateMaintenanceAdvice(alarm);
        alarm.iec61850_sent = sendIEC61850(alarm);
        alarms.push_back(alarm);
    }

    if (cavitation.stage == CavitationStage::DEVELOPED) {
        AlarmRecord alarm;
        alarm.turbine_id = cavitation.turbine_id;
        alarm.alarm_type = AlarmType::CAVITATION_DEVELOPED;
        alarm.blade_id = cavitation.blade_id;
        alarm.zone_id = cavitation.zone_id;
        alarm.timestamp_ms = ts_ms;
        alarm.measured_value = static_cast<double>(cavitation.intensity);
        alarm.threshold_value = thresholds_.cavitation_developed_limit;
        alarm.severity = AlarmSeverity::CRITICAL;
        alarm.iec61850_sent = false;

        alarm.maintenance_advice = generateMaintenanceAdvice(alarm);
        alarm.iec61850_sent = sendIEC61850(alarm);
        alarms.push_back(alarm);
    }

    return alarms;
}

bool AlarmManager::sendIEC61850(const AlarmRecord& alarm) {
    std::string report = formatIEC61850Report(alarm);
    std::cout << report << std::endl;
    return true;
}

std::string AlarmManager::formatIEC61850Report(const AlarmRecord& alarm) {
    std::ostringstream oss;

    const char* alarm_type_str = "unknown";
    switch (alarm.alarm_type) {
        case AlarmType::CAVITATION_OVERLIMIT: alarm_type_str = "cavitation_overlimit"; break;
        case AlarmType::VIBRATION_OVERLIMIT: alarm_type_str = "vibration_overlimit"; break;
        case AlarmType::FATIGUE_WARNING: alarm_type_str = "fatigue_warning"; break;
        case AlarmType::CAVITATION_DEVELOPED: alarm_type_str = "cavitation_developed"; break;
    }

    const char* severity_str = "unknown";
    switch (alarm.severity) {
        case AlarmSeverity::INFO: severity_str = "info"; break;
        case AlarmSeverity::WARNING: severity_str = "warning"; break;
        case AlarmSeverity::CRITICAL: severity_str = "critical"; break;
    }

    oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    oss << "<IEC61850 xmlns=\"urn:iec:61850:2009:MMS\">\n";
    oss << "  <Report datasetRef=\"cavitation_monitor/LLN0$dsAlarm\">\n";
    oss << "    <RptID>cavitation_alarm_report</RptID>\n";
    oss << "    <ReasonCode>quality-change</ReasonCode>\n";
    oss << "    <Timestamp>" << alarm.timestamp_ms << "</Timestamp>\n";
    oss << "    <Data>\n";
    oss << "      <DA name=\"turbine_id\" val=\"" << static_cast<int>(alarm.turbine_id) << "\"/>\n";
    oss << "      <DA name=\"alarm_type\" val=\"" << alarm_type_str << "\"/>\n";
    oss << "      <DA name=\"severity\" val=\"" << severity_str << "\"/>\n";
    oss << "      <DA name=\"blade_id\" val=\"" << static_cast<int>(alarm.blade_id) << "\"/>\n";
    oss << "      <DA name=\"zone_id\" val=\"" << static_cast<int>(alarm.zone_id) << "\"/>\n";
    oss << "      <DA name=\"measured_value\" val=\"" << std::fixed << std::setprecision(4) << alarm.measured_value << "\"/>\n";
    oss << "      <DA name=\"threshold_value\" val=\"" << std::fixed << std::setprecision(4) << alarm.threshold_value << "\"/>\n";
    oss << "    </Data>\n";
    oss << "  </Report>\n";
    oss << "</IEC61850>\n";

    return oss.str();
}

std::string AlarmManager::generateMaintenanceAdvice(const AlarmRecord& alarm) {
    switch (alarm.alarm_type) {
        case AlarmType::CAVITATION_OVERLIMIT:
            if (alarm.severity == AlarmSeverity::CRITICAL) {
                return "空化强度严重超标，建议立即停机检查转轮叶片和导叶区域，评估空蚀损伤程度，必要时更换受损部件。";
            }
            return "空化强度超出阈值，建议调整运行工况（减小开度或调整水头），安排近期巡检。";

        case AlarmType::VIBRATION_OVERLIMIT:
            if (alarm.severity == AlarmSeverity::CRITICAL) {
                return "振动严重超标，存在结构安全隐患，建议立即停机进行动平衡和机械检查。";
            }
            return "振动超标，建议检查导叶开度、转轮密封间隙，安排振动分析。";

        case AlarmType::FATIGUE_WARNING:
            if (alarm.severity == AlarmSeverity::CRITICAL) {
                return "疲劳累积损伤接近临界值，建议立即停机进行叶片无损检测，评估裂纹风险。";
            }
            return "疲劳损伤超出预警阈值，建议安排无损检测，重点检查叶片根部和出水边。";

        case AlarmType::CAVITATION_DEVELOPED:
            return "空化已发展到严重阶段，建议立即降负荷运行或停机，对转轮叶片进行全面检查和修复。";

        default:
            return "注意监测，保持当前工况运行。";
    }
}

void AlarmManager::setThresholds(const AlarmThreshold& thresholds) {
    thresholds_ = thresholds;
}

const AlarmThreshold& AlarmManager::getThresholds() const {
    return thresholds_;
}
