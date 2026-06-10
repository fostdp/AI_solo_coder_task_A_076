/**
 * Color scale utilities for the cavitation monitoring system.
 */

const CAVITATION_COLORS = {
  none: '#2ECC71',
  incipient: '#F39C12',
  critical: '#E74C3C',
  developed: '#8E44AD'
};

const ALARM_COLORS = {
  info: '#3498DB',
  warning: '#F39C12',
  critical: '#E74C3C'
};

const STAGE_LABELS = {
  none: '无空化',
  incipient: '初生空化',
  critical: '临界空化',
  developed: '发展空化'
};

/**
 * Returns the hex color for a cavitation stage.
 * @param {'none'|'incipient'|'critical'|'developed'} stage
 * @returns {string}
 */
function cavitationStageColor(stage) {
  return CAVITATION_COLORS[stage] || '#556688';
}

/**
 * Returns an RGB color interpolated from green->yellow->orange->red->purple
 * based on cavitation intensity (0-1).
 * @param {number} intensity - Value between 0 and 1
 * @returns {{r: number, g: number, b: number}}
 */
function cavitationIntensityColor(intensity) {
  var t = Math.max(0, Math.min(1, intensity));
  var stops = [
    { t: 0.0, r: 46, g: 204, b: 113 },
    { t: 0.25, r: 243, g: 196, b: 18 },
    { t: 0.5, r: 230, g: 126, b: 34 },
    { t: 0.75, r: 231, g: 76, b: 60 },
    { t: 1.0, r: 142, g: 68, b: 173 }
  ];

  for (var i = 0; i < stops.length - 1; i++) {
    if (t >= stops[i].t && t <= stops[i + 1].t) {
      var local = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
      return {
        r: Math.round(stops[i].r + (stops[i + 1].r - stops[i].r) * local),
        g: Math.round(stops[i].g + (stops[i + 1].g - stops[i].g) * local),
        b: Math.round(stops[i].b + (stops[i + 1].b - stops[i].b) * local)
      };
    }
  }
  var last = stops[stops.length - 1];
  return { r: last.r, g: last.g, b: last.b };
}

/**
 * Returns an RGB color for a spectrum/waterfall value (0-1).
 * Maps from dark blue -> cyan -> green -> yellow -> red.
 * @param {number} value - Value between 0 and 1
 * @returns {{r: number, g: number, b: number}}
 */
function spectrumColor(value) {
  var t = Math.max(0, Math.min(1, value));
  var stops = [
    { t: 0.0, r: 5, g: 10, b: 40 },
    { t: 0.25, r: 0, g: 150, b: 200 },
    { t: 0.5, r: 0, g: 210, b: 80 },
    { t: 0.75, r: 240, g: 220, b: 20 },
    { t: 1.0, r: 220, g: 40, b: 20 }
  ];

  for (var i = 0; i < stops.length - 1; i++) {
    if (t >= stops[i].t && t <= stops[i + 1].t) {
      var local = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
      return {
        r: Math.round(stops[i].r + (stops[i + 1].r - stops[i].r) * local),
        g: Math.round(stops[i].g + (stops[i + 1].g - stops[i].g) * local),
        b: Math.round(stops[i].b + (stops[i + 1].b - stops[i].b) * local)
      };
    }
  }
  var last = stops[stops.length - 1];
  return { r: last.r, g: last.g, b: last.b };
}

/**
 * Returns the hex color for an alarm severity level.
 * @param {'info'|'warning'|'critical'} severity
 * @returns {string}
 */
function alarmSeverityColor(severity) {
  return ALARM_COLORS[severity] || '#556688';
}

/**
 * Returns the Chinese label for a cavitation stage.
 * @param {'none'|'incipient'|'critical'|'developed'} stage
 * @returns {string}
 */
function stageToChineseLabel(stage) {
  return STAGE_LABELS[stage] || '未知';
}

export {
  cavitationStageColor,
  cavitationIntensityColor,
  spectrumColor,
  alarmSeverityColor,
  stageToChineseLabel
};
