/**
 * Mock data generator for the cavitation monitoring system demo.
 * Produces realistic simulated data for 6 turbines with 13 blades each.
 */

var STAGES = ['none', 'incipient', 'critical', 'developed'];
var ALARM_TYPES = ['cavitation_overlimit', 'vibration_overlimit', 'fatigue_warning', 'cavitation_developed'];
var SEVERITIES = ['info', 'warning', 'critical'];
var BLADE_COUNT = 13;
var ZONE_IDS = [1, 2, 3];
var FFT_SIZE = 256;

var TURBINE_NAMES = [
  '1号机组',
  '2号机组',
  '3号机组',
  '4号机组',
  '5号机组',
  '6号机组'
];

var MAINTENANCE_ADVICES = {
  cavitation_overlimit: '建议降低负荷运行，密切关注空化强度变化，安排停机检查',
  vibration_overlimit: '建议立即降低负荷，检查机组振动原因，必要时停机检修',
  fatigue_warning: '叶片累积损伤接近阈值，建议在下次检修时重点检查该叶片区域',
  cavitation_developed: '空化已进入发展阶段，建议尽快安排停机检修，避免叶片严重空蚀'
};

function rand(min, max) {
  return Math.random() * (max - min) + min;
}

function randInt(min, max) {
  return Math.floor(rand(min, max + 1));
}

function pick(arr) {
  return arr[randInt(0, arr.length - 1)];
}

function weightedStage() {
  var r = Math.random();
  if (r < 0.55) return 'none';
  if (r < 0.78) return 'incipient';
  if (r < 0.92) return 'critical';
  return 'developed';
}

function intensityForStage(stage) {
  switch (stage) {
    case 'none': return rand(0, 0.15);
    case 'incipient': return rand(0.15, 0.45);
    case 'critical': return rand(0.45, 0.75);
    case 'developed': return rand(0.75, 1.0);
    default: return rand(0, 0.1);
  }
}

function worstStage(stages) {
  var order = { none: 0, incipient: 1, critical: 2, developed: 3 };
  return stages.reduce(function (worst, s) {
    return order[s] > order[worst] ? s : worst;
  }, 'none');
}

/**
 * Generate blade statuses for a turbine.
 * @param {number} bladeCount - Number of blades (default 13)
 * @returns {Array<import('./types').BladeStatus>}
 */
function generateBladeStatuses(bladeCount) {
  bladeCount = bladeCount || BLADE_COUNT;
  var statuses = [];
  for (var b = 1; b <= bladeCount; b++) {
    for (var z = 0; z < ZONE_IDS.length; z++) {
      var stage = weightedStage();
      statuses.push({
        blade_id: b,
        zone_id: ZONE_IDS[z],
        stage: stage,
        intensity: Math.round(intensityForStage(stage) * 1000) / 1000,
        anomaly_score: Math.round(rand(0, stage === 'none' ? 0.2 : 0.9) * 1000) / 1000
      });
    }
  }
  return statuses;
}

/**
 * Generate statuses for all 6 turbines.
 * @returns {Array<import('./types').TurbineStatus>}
 */
function generateTurbineStatuses() {
  var turbines = [];
  for (var i = 0; i < 6; i++) {
    var bladeStatuses = generateBladeStatuses();
    var allStages = bladeStatuses.map(function (b) { return b.stage; });
    var overall = worstStage(allStages);
    turbines.push({
      id: i + 1,
      name: TURBINE_NAMES[i],
      overall_stage: overall,
      blade_statuses: bladeStatuses,
      vibration_velocity: Math.round(rand(0.5, overall === 'developed' ? 8.0 : 4.5) * 100) / 100,
      latest_update: Date.now() - randInt(0, 5000)
    });
  }
  return turbines;
}

/**
 * Generate simulated FFT spectrum data (256 points).
 * Uses sinusoidal patterns to simulate realistic cavitation noise spectra.
 * @returns {import('./types').SpectrumData}
 */
function generateSpectrumData() {
  var magnitudes = [];
  var dominantFreqBin = randInt(30, 120);
  var dominantFreqValue = 0;

  for (var i = 0; i < FFT_SIZE; i++) {
    var base = 0.02 + 0.01 * Math.random();
    var lowRise = 0.15 * Math.exp(-i / 40);
    var peak = 0.7 * Math.exp(-Math.pow(i - dominantFreqBin, 2) / (2 * 25));
    var harmonics = 0.3 * Math.exp(-Math.pow(i - dominantFreqBin * 2, 2) / (2 * 15));
    var noise = 0.05 * Math.random();
    var val = base + lowRise + peak + harmonics + noise;
    val = Math.min(1, val);
    if (val > dominantFreqValue) {
      dominantFreqValue = val;
      dominantFreqBin = i;
    }
    magnitudes.push(Math.round(val * 10000) / 10000);
  }

  var sampleRate = 51200;
  var dominantFreq = Math.round((dominantFreqBin / FFT_SIZE) * sampleRate);

  var lowPower = 0;
  var midPower = 0;
  var highPower = 0;
  for (var j = 0; j < FFT_SIZE; j++) {
    var freq = (j / FFT_SIZE) * sampleRate;
    if (freq < 2000) lowPower += magnitudes[j] * magnitudes[j];
    else if (freq < 10000) midPower += magnitudes[j] * magnitudes[j];
    else highPower += magnitudes[j] * magnitudes[j];
  }

  return {
    fft_magnitude: magnitudes,
    sample_rate: sampleRate,
    dominant_freq: dominantFreq,
    band_powers: {
      low: Math.round(lowPower * 1000) / 1000,
      mid: Math.round(midPower * 1000) / 1000,
      high: Math.round(highPower * 1000) / 1000
    }
  };
}

/**
 * Generate waterfall plot data (array of spectrum slices over time).
 * @param {number} nSlices - Number of time slices (default 30)
 * @returns {Array<import('./types').WaterfallSlice>}
 */
function generateWaterfallData(nSlices) {
  nSlices = nSlices || 30;
  var slices = [];
  var baseTime = Date.now() - nSlices * 2000;
  var baseSpectrum = generateSpectrumData().fft_magnitude;

  for (var s = 0; s < nSlices; s++) {
    var spectrum = [];
    for (var i = 0; i < baseSpectrum.length; i++) {
      var variation = 1 + (Math.random() - 0.5) * 0.15;
      var drift = 1 + 0.05 * Math.sin(s * 0.3 + i * 0.02);
      var val = Math.min(1, baseSpectrum[i] * variation * drift);
      spectrum.push(Math.round(val * 10000) / 10000);
    }
    slices.push({
      timestamp: baseTime + s * 2000,
      spectrum: spectrum
    });
  }
  return slices;
}

/**
 * Generate cavitation history over a time period.
 * @param {number} hours - Number of hours of history (default 24)
 * @returns {import('./types').CavitationHistory}
 */
function generateCavitationHistory(hours) {
  hours = hours || 24;
  var history = [];
  var now = Date.now();
  var interval = (hours * 3600000) / 120;
  var currentStage = 'none';
  var currentIntensity = rand(0, 0.1);
  var transitions = { none: 'incipient', incipient: 'critical', critical: 'developed' };

  for (var i = 0; i < 120; i++) {
    if (Math.random() < 0.05 && transitions[currentStage]) {
      currentStage = transitions[currentStage];
    }
    currentIntensity += (rand(-0.05, 0.08));
    currentIntensity = Math.max(0, Math.min(1, currentIntensity));

    if (currentIntensity < 0.15) currentStage = 'none';
    else if (currentIntensity < 0.45) currentStage = 'incipient';
    else if (currentIntensity < 0.75) currentStage = 'critical';
    else currentStage = 'developed';

    history.push({
      timestamp: now - (120 - i) * interval,
      stage: currentStage,
      intensity: Math.round(currentIntensity * 1000) / 1000
    });
  }
  return history;
}

/**
 * Generate alarm records.
 * @param {number} count - Number of alarm records (default 20)
 * @returns {import('./types').AlarmRecord[]}
 */
function generateAlarms(count) {
  count = count || 20;
  var alarms = [];
  var now = Date.now();

  for (var i = 0; i < count; i++) {
    var alarmType = pick(ALARM_TYPES);
    var severity;
    if (alarmType === 'cavitation_developed') {
      severity = pick(['critical', 'critical', 'warning']);
    } else if (alarmType === 'cavitation_overlimit') {
      severity = pick(['warning', 'critical', 'warning']);
    } else if (alarmType === 'vibration_overlimit') {
      severity = pick(['warning', 'critical']);
    } else {
      severity = pick(['info', 'warning']);
    }

    var measured = rand(0.3, 1.5);
    var threshold = measured * rand(0.6, 0.95);

    alarms.push({
      timestamp: now - randInt(0, 86400000),
      turbine_id: randInt(1, 6),
      alarm_type: alarmType,
      severity: severity,
      blade_id: randInt(1, BLADE_COUNT),
      zone_id: pick(ZONE_IDS),
      measured_value: Math.round(measured * 1000) / 1000,
      threshold_value: Math.round(threshold * 1000) / 1000,
      iec61850_sent: severity === 'critical' ? Math.random() > 0.1 : Math.random() > 0.5,
      maintenance_advice: MAINTENANCE_ADVICES[alarmType]
    });
  }

  alarms.sort(function (a, b) { return b.timestamp - a.timestamp; });
  return alarms;
}

/**
 * Generate fatigue/damage info for a blade zone.
 * @returns {import('./types').FatigueInfo}
 */
function generateFatigueInfo() {
  var cumulativeDamage = rand(0.01, 0.95);
  var remainingLife = Math.max(0, (1 - cumulativeDamage) * rand(20000, 80000));

  return {
    cumulative_damage: Math.round(cumulativeDamage * 10000) / 10000,
    remaining_life_hours: Math.round(remainingLife),
    cycle_count: randInt(100000, 5000000),
    miner_damage: Math.round(cumulativeDamage * rand(0.9, 1.1) * 10000) / 10000
  };
}

export {
  generateTurbineStatuses,
  generateSpectrumData,
  generateWaterfallData,
  generateCavitationHistory,
  generateAlarms,
  generateFatigueInfo,
  generateBladeStatuses
};
