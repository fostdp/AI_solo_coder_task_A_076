/**
 * @typedef {'none'|'incipient'|'critical'|'developed'} CavitationStage
 */

/**
 * @typedef {'info'|'warning'|'critical'} AlarmSeverity
 */

/**
 * @typedef {'cavitation_overlimit'|'vibration_overlimit'|'fatigue_warning'|'cavitation_developed'} AlarmType
 */

/**
 * @typedef {Object} BladeStatus
 * @property {number} blade_id - Blade identifier (1-13)
 * @property {number} zone_id - Zone on blade (1=inlet, 2=mid, 3=outlet)
 * @property {CavitationStage} stage - Current cavitation stage
 * @property {number} intensity - Cavitation intensity 0-1
 * @property {number} anomaly_score - Anomaly detection score 0-1
 */

/**
 * @typedef {Object} TurbineStatus
 * @property {number} id - Turbine identifier (1-6)
 * @property {string} name - Turbine display name
 * @property {CavitationStage} overall_stage - Worst cavitation stage across all blades
 * @property {BladeStatus[]} blade_statuses - Array of blade zone statuses
 * @property {number} vibration_velocity - Current vibration velocity in mm/s
 * @property {number} latest_update - Timestamp of last update (ms since epoch)
 */

/**
 * @typedef {Object} BandPowers
 * @property {number} low - Low frequency band power (0-2kHz)
 * @property {number} mid - Mid frequency band power (2-10kHz)
 * @property {number} high - High frequency band power (10-50kHz)
 */

/**
 * @typedef {Object} SpectrumData
 * @property {number[]} fft_magnitude - FFT magnitude array (256 points)
 * @property {number} sample_rate - Sample rate in Hz
 * @property {number} dominant_freq - Dominant frequency in Hz
 * @property {BandPowers} band_powers - Power in frequency bands
 */

/**
 * @typedef {Object} WaterfallSlice
 * @property {number} timestamp - Timestamp in ms since epoch
 * @property {number[]} spectrum - Spectrum magnitude array for this time slice
 */

/**
 * @typedef {Object} FatigueInfo
 * @property {number} cumulative_damage - Cumulative fatigue damage (Miner's rule, 0-1+)
 * @property {number} remaining_life_hours - Estimated remaining life in hours
 * @property {number} cycle_count - Total stress cycle count
 * @property {number} miner_damage - Current Miner cumulative damage index
 */

/**
 * @typedef {Object} AlarmRecord
 * @property {number} timestamp - Alarm timestamp in ms since epoch
 * @property {number} turbine_id - Turbine identifier
 * @property {AlarmType} alarm_type - Type of alarm
 * @property {AlarmSeverity} severity - Alarm severity level
 * @property {number} blade_id - Blade identifier
 * @property {number} zone_id - Zone on blade
 * @property {number} measured_value - The measured value that triggered the alarm
 * @property {number} threshold_value - The threshold that was exceeded
 * @property {boolean} iec61850_sent - Whether IEC 61850 notification was sent
 * @property {string} maintenance_advice - Recommended maintenance action
 */

/**
 * @typedef {Object} CavitationHistoryPoint
 * @property {number} timestamp - Timestamp in ms since epoch
 * @property {CavitationStage} stage - Cavitation stage at this point
 * @property {number} intensity - Cavitation intensity at this point
 */

/** @typedef {CavitationHistoryPoint[]} CavitationHistory */
