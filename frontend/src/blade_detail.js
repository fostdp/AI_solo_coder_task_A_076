import { cavitationStageColor, stageToChineseLabel } from './colorScales.js';

function formatDuration(hours) {
    if (hours <= 0) return '0h';
    if (hours < 24) return Math.round(hours) + 'h';
    if (hours < 8760) return (hours / 24).toFixed(1) + '天';
    return (hours / 8760).toFixed(1) + '年';
}

function stageBadgeHTML(stage) {
    return '<span class="badge badge-' + stage + '">' + stageToChineseLabel(stage) + '</span>';
}

function severityBadgeHTML(sev) {
    var cls = sev === 'critical' ? 'badge-critical-alarm' : 'badge-' + sev;
    var labels = { info: '提示', warning: '警告', critical: '紧急' };
    return '<span class="badge ' + cls + '">' + labels[sev] + '</span>';
}

class BladeDetailPanel {
    constructor(container) {
        this._container = container;
        this._panelEl = null;
        this._overlayEl = null;
        this._updateCallback = null;
        this._bladeId = null;
        this._bladeStatuses = null;
        this._fatigueInfo = null;
        this._cavitationHistory = null;
    }

    show(bladeId, bladeStatuses, fatigueInfo, cavitationHistory) {
        this._bladeId = bladeId;
        this._bladeStatuses = bladeStatuses;
        this._fatigueInfo = fatigueInfo;
        this._cavitationHistory = cavitationHistory;

        this.hide();

        var overlay = document.createElement('div');
        overlay.className = 'panel-overlay';
        overlay.id = 'panel-overlay';
        this._overlayEl = overlay;

        var panel = document.createElement('div');
        panel.className = 'detail-panel';
        panel.id = 'blade-panel';
        this._panelEl = panel;

        panel.innerHTML =
            '<div class="detail-panel-header">' +
                '<div class="detail-panel-title" id="blade-panel-title">叶片 #' + bladeId + ' 详情</div>' +
                '<button class="detail-panel-close" id="blade-panel-close">✕</button>' +
            '</div>' +
            '<div class="detail-panel-body" id="blade-panel-body"></div>';

        this._container.appendChild(overlay);
        this._container.appendChild(panel);

        overlay.addEventListener('click', function () {
            this.hide();
        }.bind(this));

        var closeBtn = panel.querySelector('#blade-panel-close');
        if (closeBtn) {
            closeBtn.addEventListener('click', function () {
                this.hide();
            }.bind(this));
        }

        this._renderBody();
    }

    _renderBody() {
        var body = this._panelEl ? this._panelEl.querySelector('#blade-panel-body') : null;
        if (!body) return;

        var bladeId = this._bladeId;
        var bladeStatuses = this._bladeStatuses;
        var fatigueInfo = this._fatigueInfo;
        var cavitationHistory = this._cavitationHistory;

        var zonesHTML = bladeStatuses.map(function (bs) {
            var zoneNames = { 1: '进口边', 2: '通道中', 3: '出口边' };
            return '<div style="display:flex;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(30,58,95,0.3)">' +
                '<div style="display:flex;align-items:center;gap:8px">' +
                    '<span style="font-size:13px;color:var(--text-primary)">' + (zoneNames[bs.zone_id] || '区域' + bs.zone_id) + '</span>' +
                    stageBadgeHTML(bs.stage) +
                '</div>' +
                '<div style="font-size:12px;font-family:var(--font-mono);color:var(--text-secondary)">' + (bs.intensity * 100).toFixed(1) + '%</div>' +
            '</div>';
        }).join('');

        var fatigue = fatigueInfo;
        var damagePct = fatigue ? (fatigue.cumulative_damage * 100) : 0;
        var barClass = damagePct > 75 ? 'purple' : damagePct > 50 ? 'red' : damagePct > 25 ? 'yellow' : 'green';

        var histCanvasId = 'history-canvas';

        body.innerHTML =
            '<div style="margin-bottom:16px">' +
                '<div style="font-size:13px;font-weight:600;color:var(--text-primary);margin-bottom:8px">各区域空化状态</div>' +
                zonesHTML +
            '</div>' +
            '<div style="margin-bottom:16px">' +
                '<div style="font-size:13px;font-weight:600;color:var(--text-primary);margin-bottom:8px">疲劳损伤评估</div>' +
                '<div style="display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:10px">' +
                    '<div style="background:var(--bg-primary);border-radius:var(--radius-md);padding:10px;text-align:center">' +
                        '<div style="font-size:10px;color:var(--text-muted)">累积损伤</div>' +
                        '<div style="font-size:18px;font-weight:700;font-family:var(--font-mono);color:' + (damagePct > 50 ? 'var(--cav-critical)' : 'var(--text-primary)') + '">' + damagePct.toFixed(2) + '%</div>' +
                    '</div>' +
                    '<div style="background:var(--bg-primary);border-radius:var(--radius-md);padding:10px;text-align:center">' +
                        '<div style="font-size:10px;color:var(--text-muted)">剩余寿命</div>' +
                        '<div style="font-size:18px;font-weight:700;font-family:var(--font-mono);color:var(--accent)">' + (fatigue ? formatDuration(fatigue.remaining_life_hours) : '—') + '</div>' +
                    '</div>' +
                '</div>' +
                '<div class="fatigue-bar">' +
                    '<div class="fatigue-label"><span>Miner累积损伤</span><span>' + damagePct.toFixed(2) + '%</span></div>' +
                    '<div class="progress-bar" style="height:8px;margin-top:4px">' +
                        '<div class="progress-bar-fill ' + barClass + '" style="width:' + Math.min(100, damagePct) + '%"></div>' +
                    '</div>' +
                '</div>' +
                '<div style="margin-top:8px;font-size:11px;color:var(--text-muted);font-family:var(--font-mono)">循环次数: ' + (fatigue ? fatigue.cycle_count.toLocaleString() : '—') + '</div>' +
            '</div>' +
            '<div style="margin-bottom:16px">' +
                '<div style="font-size:13px;font-weight:600;color:var(--text-primary);margin-bottom:8px">空化历史趋势 (24h)</div>' +
                '<div class="canvas-container" style="aspect-ratio:2/1">' +
                    '<canvas id="' + histCanvasId + '" style="width:100%;height:100%"></canvas>' +
                '</div>' +
            '</div>' +
            '<div>' +
                '<div style="font-size:13px;font-weight:600;color:var(--text-primary);margin-bottom:8px">异常分数</div>' +
                '<div style="display:flex;gap:10px">' +
                    bladeStatuses.map(function (bs) {
                        return '<div style="flex:1;text-align:center;background:var(--bg-primary);border-radius:var(--radius-md);padding:8px">' +
                            '<div style="font-size:10px;color:var(--text-muted)">区域' + bs.zone_id + '</div>' +
                            '<div style="font-size:16px;font-weight:700;font-family:var(--font-mono);color:' + (bs.anomaly_score > 0.5 ? 'var(--cav-critical)' : 'var(--text-primary)') + '">' + bs.anomaly_score.toFixed(3) + '</div>' +
                        '</div>';
                    }).join('') +
                '</div>' +
            '</div>';

        requestAnimationFrame(function () {
            this.drawHistoryChart(histCanvasId, cavitationHistory);
        }.bind(this));
    }

    drawHistoryChart(canvasId, history) {
        var canvas = document.getElementById(canvasId);
        if (!canvas) return;

        var rect = canvas.parentElement.getBoundingClientRect();
        var dpr = window.devicePixelRatio || 1;
        canvas.width = rect.width * dpr;
        canvas.height = rect.height * dpr;
        var ctx = canvas.getContext('2d');
        ctx.scale(dpr, dpr);

        if (!history || history.length === 0) return;

        var margin = { top: 10, right: 10, bottom: 24, left: 40 };
        var plotW = rect.width - margin.left - margin.right;
        var plotH = rect.height - margin.top - margin.bottom;

        ctx.strokeStyle = '#1E3A5F';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(margin.left, margin.top);
        ctx.lineTo(margin.left, margin.top + plotH);
        ctx.lineTo(margin.left + plotW, margin.top + plotH);
        ctx.stroke();

        ctx.font = '9px "JetBrains Mono", monospace';
        ctx.fillStyle = '#556688';
        ctx.textAlign = 'right';
        for (var v = 0; v <= 4; v++) {
            var vy = margin.top + plotH - (v / 4) * plotH;
            ctx.fillText((v * 25) + '%', margin.left - 4, vy + 3);
        }

        var stageYMap = { none: 0.1, incipient: 0.35, critical: 0.65, developed: 0.9 };

        ctx.beginPath();
        ctx.strokeStyle = '#00D4FF';
        ctx.lineWidth = 1.5;
        for (var i = 0; i < history.length; i++) {
            var x = margin.left + (i / (history.length - 1)) * plotW;
            var y = margin.top + plotH - history[i].intensity * plotH;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        ctx.beginPath();
        ctx.fillStyle = 'rgba(0, 212, 255, 0.08)';
        for (var j = 0; j < history.length; j++) {
            var hx = margin.left + (j / (history.length - 1)) * plotW;
            var hy = margin.top + plotH - history[j].intensity * plotH;
            if (j === 0) ctx.moveTo(hx, hy);
            else ctx.lineTo(hx, hy);
        }
        ctx.lineTo(margin.left + plotW, margin.top + plotH);
        ctx.lineTo(margin.left, margin.top + plotH);
        ctx.closePath();
        ctx.fill();

        var stages = ['none', 'incipient', 'critical', 'developed'];
        var stageColors = { none: 'rgba(46,204,113,0.15)', incipient: 'rgba(243,156,18,0.15)', critical: 'rgba(231,76,60,0.15)', developed: 'rgba(142,68,173,0.15)' };
        for (var s = 0; s < stages.length; s++) {
            var sy0 = margin.top + plotH - stageYMap[stages[s]] * plotH - plotH * 0.1;
            var sy1 = margin.top + plotH - (s < stages.length - 1 ? stageYMap[stages[s + 1]] : 1) * plotH + plotH * 0.1;
            ctx.fillStyle = stageColors[stages[s]];
            ctx.fillRect(margin.left, Math.max(margin.top, sy1), plotW, Math.min(plotH, sy0 - sy1));
        }
    }

    hide() {
        if (this._panelEl) {
            this._panelEl.remove();
            this._panelEl = null;
        }
        if (this._overlayEl) {
            this._overlayEl.remove();
            this._overlayEl = null;
        }
    }

    isVisible() {
        return this._panelEl !== null && this._panelEl.parentNode !== null;
    }

    onUpdate(callback) {
        this._updateCallback = callback;
    }
}

export { BladeDetailPanel, formatDuration, stageBadgeHTML, severityBadgeHTML };
