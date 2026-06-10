import { cavitationStageColor, alarmSeverityColor, stageToChineseLabel } from './colorScales.js';
import { drawTurbineProfile, drawSensorMarkers, drawBladeInfo, hitTestBlade, BLADE_COUNT } from './canvasRenderer.js';
import { initWaterfallGL, updateWaterfallData, renderWaterfall, drawWaterfallOverlay, cleanupWaterfallGL, createWaterfallWorker, MAX_SLICES } from './webglRenderer.js';
import { generateTurbineStatuses, generateSpectrumData, generateWaterfallData, generateCavitationHistory, generateAlarms, generateFatigueInfo } from './mockData.js';

var state = {
    currentPage: 'dashboard',
    turbines: [],
    selectedTurbine: null,
    selectedBlade: null,
    bladePanelOpen: false,
    alarms: [],
    waterfallGL: null,
    waterfallWorker: null,
    waterfallPendingVertex: null,
    waterfallData: [],
    spectrumData: null,
    cavitationHistory: [],
    fatigueInfo: null,
    animFrame: null,
    rotationAngle: 0,
    updateInterval: null
};

function $(sel) { return document.querySelector(sel); }
function $$(sel) { return document.querySelectorAll(sel); }

function init() {
    state.turbines = generateTurbineStatuses();
    state.alarms = generateAlarms(25);
    state.waterfallData = generateWaterfallData(30);
    state.spectrumData = generateSpectrumData();

    setupNavigation();
    setupSidebar();
    resolveInitialRoute();
    renderPage();
    startAutoRefresh();
}

function resolveInitialRoute() {
    var hash = location.hash.slice(1) || 'dashboard';
    if (hash.startsWith('turbine/')) {
        var parts = hash.split('/');
        state.currentPage = 'turbine-detail';
        state.selectedTurbine = parseInt(parts[1]) || 1;
    } else if (hash === 'alarms') {
        state.currentPage = 'alarms';
    } else {
        state.currentPage = 'dashboard';
    }
}

function setupNavigation() {
    window.addEventListener('hashchange', function () {
        var hash = location.hash.slice(1) || 'dashboard';
        if (hash.startsWith('turbine/')) {
            var parts = hash.split('/');
            state.currentPage = 'turbine-detail';
            state.selectedTurbine = parseInt(parts[1]) || 1;
        } else {
            state.currentPage = hash;
            state.selectedTurbine = null;
            state.selectedBlade = null;
            state.bladePanelOpen = false;
        }
        renderPage();
    });
}

function setupSidebar() {
    var toggleBtn = $('#sidebar-toggle');
    if (toggleBtn) {
        toggleBtn.addEventListener('click', function () {
            var sidebar = $('.sidebar');
            if (sidebar) sidebar.classList.toggle('collapsed');
        });
    }
}

function navigate(page, params) {
    if (page === 'turbine-detail') {
        location.hash = 'turbine/' + (params || 1);
    } else {
        location.hash = page;
    }
}

function renderPage() {
    var content = $('#main-content');
    if (!content) return;

    stopAnimation();

    switch (state.currentPage) {
        case 'dashboard':
            renderDashboard(content);
            break;
        case 'turbine-detail':
            renderTurbineDetail(content);
            break;
        case 'alarms':
            renderAlarmsPage(content);
            break;
        default:
            renderDashboard(content);
    }

    updateNavActive();
    updateHeaderTitle();
}

function stopAnimation() {
    if (state.animFrame) {
        cancelAnimationFrame(state.animFrame);
        state.animFrame = null;
    }
    if (state.waterfallGL) {
        cleanupWaterfallGL(state.waterfallGL);
        state.waterfallGL = null;
    }
    if (state.waterfallWorker) {
        state.waterfallWorker.terminate();
        state.waterfallWorker = null;
    }
    state.waterfallPendingVertex = null;
}

function startAutoRefresh() {
    if (state.updateInterval) clearInterval(state.updateInterval);
    state.updateInterval = setInterval(function () {
        state.turbines = generateTurbineStatuses();
        if (state.currentPage === 'dashboard') {
            updateDashboardCards();
        } else if (state.currentPage === 'turbine-detail') {
            state.waterfallData = generateWaterfallData(30);
            state.spectrumData = generateSpectrumData();
            dispatchWaterfallUpdate();
        }
    }, 3000);
}

function updateNavActive() {
    $$('.nav-item').forEach(function (item) {
        item.classList.remove('active');
        var target = item.getAttribute('data-page');
        if (target === state.currentPage) {
            item.classList.add('active');
        } else if (state.currentPage === 'turbine-detail' && target === 'dashboard') {
            // keep dashboard highlighted as parent
        }
    });
}

function updateHeaderTitle() {
    var title = $('#header-title');
    if (!title) return;
    switch (state.currentPage) {
        case 'dashboard':
            title.textContent = '监测总览';
            break;
        case 'turbine-detail':
            title.textContent = state.selectedTurbine ? state.selectedTurbine + '号机组 — 空化监测详情' : '机组详情';
            break;
        case 'alarms':
            title.textContent = '告警管理';
            break;
    }
}

function formatTime(ts) {
    var d = new Date(ts);
    return d.getFullYear() + '-' +
        String(d.getMonth() + 1).padStart(2, '0') + '-' +
        String(d.getDate()).padStart(2, '0') + ' ' +
        String(d.getHours()).padStart(2, '0') + ':' +
        String(d.getMinutes()).padStart(2, '0') + ':' +
        String(d.getSeconds()).padStart(2, '0');
}

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

function alarmTypeLabel(type) {
    var labels = {
        cavitation_overlimit: '空化超限',
        vibration_overlimit: '振动超标',
        fatigue_warning: '疲劳预警',
        cavitation_developed: '空化发展'
    };
    return labels[type] || type;
}

function countBladesByStage(bladeStatuses, stage) {
    var count = 0;
    var seen = {};
    for (var i = 0; i < bladeStatuses.length; i++) {
        var b = bladeStatuses[i];
        if (b.stage === stage && !seen[b.blade_id]) {
            seen[b.blade_id] = true;
            count++;
        }
    }
    return count;
}

function renderDashboard(container) {
    var turbineCards = state.turbines.map(function (t) {
        var incipientCount = countBladesByStage(t.blade_statuses, 'incipient');
        var criticalCount = countBladesByStage(t.blade_statuses, 'critical');
        var developedCount = countBladesByStage(t.blade_statuses, 'developed');
        return '<div class="card card-glow turbine-card animate-fade-in" data-turbine="' + t.id + '" style="cursor:pointer;animation-delay:' + (t.id * 0.08) + 's">' +
            '<div class="card-header">' +
                '<div>' +
                    '<div class="card-title">' + t.name + '</div>' +
                    '<div class="card-subtitle">混流式水轮机 · ' + t.blade_statuses.length + '监测点</div>' +
                '</div>' +
                '<div class="status-dot status-dot-' + t.overall_stage + '"></div>' +
            '</div>' +
            '<div style="margin-top:12px">' +
                '<div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:8px">' +
                    '<span style="font-size:12px;color:var(--text-muted)">空化状态</span>' +
                    stageBadgeHTML(t.overall_stage) +
                '</div>' +
                '<div style="display:flex;gap:12px;font-size:11px;font-family:var(--font-mono);color:var(--text-secondary)">' +
                    '<span>初生: ' + incipientCount + '</span>' +
                    '<span>临界: ' + criticalCount + '</span>' +
                    '<span style="color:var(--cav-developed)">发展: ' + developedCount + '</span>' +
                '</div>' +
            '</div>' +
            '<div style="margin-top:12px;display:flex;justify-content:space-between;font-size:12px;color:var(--text-secondary)">' +
                '<span>振动速度</span>' +
                '<span class="text-mono" style="color:' + (t.vibration_velocity > 5 ? 'var(--alarm-warning)' : 'var(--text-primary)') + '">' + t.vibration_velocity.toFixed(2) + ' mm/s</span>' +
            '</div>' +
            '<div class="progress-bar" style="margin-top:6px">' +
                '<div class="progress-bar-fill ' + (t.vibration_velocity > 7 ? 'red' : t.vibration_velocity > 4 ? 'yellow' : 'green') + '" style="width:' + Math.min(100, t.vibration_velocity / 10 * 100) + '%"></div>' +
            '</div>' +
            '<div style="margin-top:10px;font-size:11px;color:var(--text-muted)">更新: ' + formatTime(t.latest_update) + '</div>' +
        '</div>';
    }).join('');

    var recentAlarms = state.alarms.slice(0, 5).map(function (a) {
        return '<div style="display:flex;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid rgba(30,58,95,0.3)">' +
            severityBadgeHTML(a.severity) +
            '<div style="flex:1;min-width:0">' +
                '<div style="font-size:13px;color:var(--text-primary);white-space:nowrap;overflow:hidden;text-overflow:ellipsis">' + a.turbine_id + '号机组 · ' + alarmTypeLabel(a.alarm_type) + '</div>' +
                '<div style="font-size:11px;color:var(--text-muted)">' + formatTime(a.timestamp) + '</div>' +
            '</div>' +
        '</div>';
    }).join('');

    var alarmStats = { info: 0, warning: 0, critical: 0 };
    state.alarms.forEach(function (a) { alarmStats[a.severity]++; });

    container.innerHTML =
        '<div class="grid-3" id="turbine-cards">' + turbineCards + '</div>' +
        '<div style="margin-top:24px;display:grid;grid-template-columns:1fr 1fr;gap:20px">' +
            '<div class="card">' +
                '<div class="card-header"><div class="card-title">最近告警</div></div>' +
                recentAlarms +
                '<div style="margin-top:12px"><button class="header-btn" onclick="window._appNavigate(\'alarms\')">查看全部告警 →</button></div>' +
            '</div>' +
            '<div class="card">' +
                '<div class="card-header"><div class="card-title">告警统计</div></div>' +
                '<div style="display:grid;grid-template-columns:repeat(3,1fr);gap:16px;margin-top:8px">' +
                    '<div style="text-align:center">' +
                        '<div style="font-size:28px;font-weight:700;color:var(--alarm-info);font-family:var(--font-mono)">' + alarmStats.info + '</div>' +
                        '<div style="font-size:12px;color:var(--text-muted);margin-top:4px">提示</div>' +
                    '</div>' +
                    '<div style="text-align:center">' +
                        '<div style="font-size:28px;font-weight:700;color:var(--alarm-warning);font-family:var(--font-mono)">' + alarmStats.warning + '</div>' +
                        '<div style="font-size:12px;color:var(--text-muted);margin-top:4px">警告</div>' +
                    '</div>' +
                    '<div style="text-align:center">' +
                        '<div style="font-size:28px;font-weight:700;color:var(--alarm-critical);font-family:var(--font-mono)">' + alarmStats.critical + '</div>' +
                        '<div style="font-size:12px;color:var(--text-muted);margin-top:4px">紧急</div>' +
                    '</div>' +
                '</div>' +
                '<div style="margin-top:20px">' +
                    '<div style="display:flex;justify-content:space-between;font-size:12px;color:var(--text-secondary);margin-bottom:6px"><span>IEC 61850 推送率</span><span class="text-mono">' +
                        (state.alarms.length > 0 ? Math.round(state.alarms.filter(function(a){return a.iec61850_sent}).length / state.alarms.length * 100) : 0) + '%</span></div>' +
                    '<div class="progress-bar"><div class="progress-bar-fill green" style="width:' +
                        (state.alarms.length > 0 ? state.alarms.filter(function(a){return a.iec61850_sent}).length / state.alarms.length * 100 : 0) + '%"></div></div>' +
                '</div>' +
            '</div>' +
        '</div>';

    $$('.turbine-card').forEach(function (card) {
        card.addEventListener('click', function () {
            var tid = card.getAttribute('data-turbine');
            navigate('turbine-detail', tid);
        });
    });
}

function updateDashboardCards() {
    if (state.currentPage !== 'dashboard') return;
    var cardsContainer = $('#turbine-cards');
    if (cardsContainer) {
        renderDashboard($('#main-content'));
    }
}

function renderTurbineDetail(container) {
    var turbine = state.turbines[state.selectedTurbine - 1] || state.turbines[0];

    container.innerHTML =
        '<div style="display:grid;grid-template-columns:1fr 1fr;gap:20px">' +
            '<div>' +
                '<div class="card" style="padding:12px">' +
                    '<div class="card-header" style="margin-bottom:8px"><div class="card-title">水轮机剖面图 — 空化云图</div></div>' +
                    '<div class="canvas-container" style="aspect-ratio:4/3">' +
                        '<canvas id="turbine-canvas" style="width:100%;height:100%"></canvas>' +
                    '</div>' +
                    '<div style="display:flex;gap:16px;margin-top:8px;font-size:11px;align-items:center">' +
                        '<div style="display:flex;align-items:center;gap:4px"><span class="status-dot status-dot-none"></span> 无空化</div>' +
                        '<div style="display:flex;align-items:center;gap:4px"><span class="status-dot status-dot-incipient"></span> 初生</div>' +
                        '<div style="display:flex;align-items:center;gap:4px"><span class="status-dot status-dot-critical"></span> 临界</div>' +
                        '<div style="display:flex;align-items:center;gap:4px"><span class="status-dot status-dot-developed"></span> 发展</div>' +
                    '</div>' +
                '</div>' +
                '<div class="card" style="margin-top:20px;padding:12px">' +
                    '<div class="card-header" style="margin-bottom:8px"><div class="card-title">实时指标</div></div>' +
                    '<div id="realtime-metrics" style="display:grid;grid-template-columns:repeat(3,1fr);gap:12px"></div>' +
                '</div>' +
            '</div>' +
            '<div>' +
                '<div class="card" style="padding:12px">' +
                    '<div class="card-header" style="margin-bottom:8px"><div class="card-title">噪声频谱瀑布图</div></div>' +
                    '<div class="canvas-container" style="aspect-ratio:4/3;position:relative">' +
                        '<canvas id="waterfall-canvas" style="width:100%;height:100%"></canvas>' +
                        '<canvas id="waterfall-overlay" style="position:absolute;top:0;left:0;width:100%;height:100%;pointer-events:none"></canvas>' +
                    '</div>' +
                '</div>' +
                '<div class="card" style="margin-top:20px;padding:12px">' +
                    '<div class="card-header" style="margin-bottom:8px"><div class="card-title">频谱分析</div></div>' +
                    '<div class="canvas-container" style="aspect-ratio:3/1">' +
                        '<canvas id="spectrum-canvas" style="width:100%;height:100%"></canvas>' +
                    '</div>' +
                '</div>' +
            '</div>' +
        '</div>' +
        (state.bladePanelOpen ? renderBladePanel() : '');

    requestAnimationFrame(function () {
        initTurbineCanvas(turbine);
        initWaterfallCanvas();
        initSpectrumCanvas();
        renderRealtimeMetrics(turbine);
    });
}

function initTurbineCanvas(turbine) {
    var canvas = $('#turbine-canvas');
    if (!canvas) return;

    var rect = canvas.parentElement.getBoundingClientRect();
    var dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    var ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);

    function animate(time) {
        ctx.clearRect(0, 0, rect.width, rect.height);
        drawTurbineProfile(ctx, rect.width, rect.height, turbine.blade_statuses, state.selectedBlade, time);
        drawSensorMarkers(ctx, rect.width / 2, rect.height * 0.42, Math.min(rect.width, rect.height) * 0.22, Math.min(rect.width, rect.height) * 0.37);

        if (state.selectedBlade && state.bladePanelOpen) {
            var cx = rect.width / 2;
            var cy = rect.height * 0.42;
            var r = Math.min(rect.width, rect.height) * 0.22;
            var angle = ((state.selectedBlade - 1) / BLADE_COUNT) * Math.PI * 2 - Math.PI / 2;
            var bx = cx + r * 0.6 * Math.cos(angle);
            var by = cy + r * 0.6 * Math.sin(angle);
            var bladeSt = null;
            for (var i = 0; i < turbine.blade_statuses.length; i++) {
                if (turbine.blade_statuses[i].blade_id === state.selectedBlade && turbine.blade_statuses[i].zone_id === 1) {
                    bladeSt = turbine.blade_statuses[i];
                    break;
                }
            }
            drawBladeInfo(ctx, bx, by, state.selectedBlade, bladeSt, state.fatigueInfo);
        }

        state.animFrame = requestAnimationFrame(animate);
    }

    canvas.addEventListener('click', function (e) {
        var canvasRect = canvas.getBoundingClientRect();
        var x = e.clientX - canvasRect.left;
        var y = e.clientY - canvasRect.top;
        var cx = rect.width / 2;
        var cy = rect.height * 0.42;
        var r = Math.min(rect.width, rect.height) * 0.22;

        var bladeId = hitTestBlade(x, y, cx, cy, r, BLADE_COUNT);
        if (bladeId > 0) {
            state.selectedBlade = bladeId;
            state.bladePanelOpen = true;
            state.cavitationHistory = generateCavitationHistory(24);
            state.fatigueInfo = generateFatigueInfo();
            renderBladePanelContent();
        }
    });

    state.animFrame = requestAnimationFrame(animate);
}

function initWaterfallCanvas() {
    var canvas = $('#waterfall-canvas');
    if (!canvas) return;

    var rect = canvas.parentElement.getBoundingClientRect();
    var dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;

    state.waterfallGL = initWaterfallGL(canvas);

    if (state.waterfallWorker) {
        state.waterfallWorker.terminate();
    }
    state.waterfallWorker = createWaterfallWorker();
    state.waterfallWorker.onmessage = function(e) {
        if (e.data.vertexData) {
            state.waterfallPendingVertex = new Float32Array(e.data.vertexData);
        }
    };

    dispatchWaterfallUpdate();

    var overlayCanvas = $('#waterfall-overlay');
    if (overlayCanvas) {
        overlayCanvas.width = rect.width * dpr;
        overlayCanvas.height = rect.height * dpr;
        var octx = overlayCanvas.getContext('2d');
        octx.scale(dpr, dpr);
        drawWaterfallOverlay(octx, { width: rect.width, height: rect.height });
    }

    function animateWaterfall() {
        if (state.waterfallPendingVertex && state.waterfallGL) {
            updateWaterfallData(state.waterfallGL, null, state.waterfallPendingVertex);
            state.waterfallPendingVertex = null;
        }
        if (state.waterfallGL) {
            state.rotationAngle += 0.003;
            renderWaterfall(state.waterfallGL, state.rotationAngle);
        }
        if (state.currentPage === 'turbine-detail') {
            requestAnimationFrame(animateWaterfall);
        }
    }
    requestAnimationFrame(animateWaterfall);
}

function dispatchWaterfallUpdate() {
    if (!state.waterfallWorker || !state.waterfallData || state.waterfallData.length === 0) return;

    var slicesToSend = state.waterfallData.slice(-MAX_SLICES);
    var transferable = slicesToSend.map(function(s) {
        return { spectrum: s.spectrum };
    });

    state.waterfallWorker.postMessage({ slices: transferable });
}

function initSpectrumCanvas() {
    var canvas = $('#spectrum-canvas');
    if (!canvas) return;

    var rect = canvas.parentElement.getBoundingClientRect();
    var dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    var ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);

    drawSpectrumPlot(ctx, rect.width, rect.height, state.spectrumData);
}

function drawSpectrumPlot(ctx, width, height, data) {
    if (!data) return;
    ctx.clearRect(0, 0, width, height);

    var margin = { top: 10, right: 20, bottom: 30, left: 50 };
    var plotW = width - margin.left - margin.right;
    var plotH = height - margin.top - margin.bottom;
    var mags = data.fft_magnitude;
    var n = mags.length;
    var maxVal = 0;
    for (var i = 0; i < n; i++) { if (mags[i] > maxVal) maxVal = mags[i]; }
    maxVal = maxVal || 1;

    ctx.strokeStyle = '#1E3A5F';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(margin.left, margin.top);
    ctx.lineTo(margin.left, margin.top + plotH);
    ctx.lineTo(margin.left + plotW, margin.top + plotH);
    ctx.stroke();

    ctx.font = '10px "JetBrains Mono", monospace';
    ctx.fillStyle = '#556688';
    ctx.textAlign = 'center';
    for (var f = 0; f <= 4; f++) {
        var fx = margin.left + (f / 4) * plotW;
        ctx.fillText((f * data.sample_rate / 4 / 1000).toFixed(0) + 'k', fx, margin.top + plotH + 16);
    }

    ctx.textAlign = 'right';
    for (var v = 0; v <= 4; v++) {
        var vy = margin.top + plotH - (v / 4) * plotH;
        ctx.fillText((v / 4 * maxVal).toFixed(2), margin.left - 6, vy + 3);
    }

    var grad = ctx.createLinearGradient(margin.left, 0, margin.left + plotW, 0);
    grad.addColorStop(0, '#00D4FF');
    grad.addColorStop(0.5, '#2ECC71');
    grad.addColorStop(1, '#F39C12');

    ctx.beginPath();
    ctx.strokeStyle = grad;
    ctx.lineWidth = 1.5;
    for (var j = 0; j < n; j++) {
        var x = margin.left + (j / n) * plotW;
        var y = margin.top + plotH - (mags[j] / maxVal) * plotH;
        if (j === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();

    ctx.beginPath();
    ctx.fillStyle = 'rgba(0, 212, 255, 0.1)';
    for (var k = 0; k < n; k++) {
        var sx = margin.left + (k / n) * plotW;
        var sy = margin.top + plotH - (mags[k] / maxVal) * plotH;
        if (k === 0) ctx.moveTo(sx, sy);
        else ctx.lineTo(sx, sy);
    }
    ctx.lineTo(margin.left + plotW, margin.top + plotH);
    ctx.lineTo(margin.left, margin.top + plotH);
    ctx.closePath();
    ctx.fill();
}

function renderRealtimeMetrics(turbine) {
    var container = $('#realtime-metrics');
    if (!container) return;

    var metrics = [
        { label: '振动速度', value: turbine.vibration_velocity.toFixed(2), unit: 'mm/s', color: turbine.vibration_velocity > 5 ? 'var(--alarm-warning)' : 'var(--accent)' },
        { label: '主频', value: state.spectrumData ? state.spectrumData.dominant_freq : '—', unit: 'Hz', color: 'var(--accent)' },
        { label: '低频功率', value: state.spectrumData ? state.spectrumData.band_powers.low.toFixed(3) : '—', unit: '', color: 'var(--cav-none)' },
        { label: '中频功率', value: state.spectrumData ? state.spectrumData.band_powers.mid.toFixed(3) : '—', unit: '', color: 'var(--cav-incipient)' },
        { label: '高频功率', value: state.spectrumData ? state.spectrumData.band_powers.high.toFixed(3) : '—', unit: '', color: 'var(--cav-critical)' },
        { label: '空化叶片', value: countBladesByStage(turbine.blade_statuses, 'incipient') + countBladesByStage(turbine.blade_statuses, 'critical') + countBladesByStage(turbine.blade_statuses, 'developed'), unit: '/13', color: 'var(--cav-developed)' }
    ];

    container.innerHTML = metrics.map(function (m) {
        return '<div style="background:var(--bg-primary);border-radius:var(--radius-md);padding:12px;text-align:center">' +
            '<div style="font-size:11px;color:var(--text-muted);margin-bottom:6px">' + m.label + '</div>' +
            '<div style="font-size:20px;font-weight:700;color:' + m.color + ';font-family:var(--font-mono)">' + m.value + '<span style="font-size:11px;color:var(--text-muted);margin-left:4px">' + m.unit + '</span></div>' +
        '</div>';
    }).join('');
}

function renderBladePanel() {
    return '<div class="panel-overlay" id="panel-overlay"></div>' +
        '<div class="detail-panel" id="blade-panel">' +
            '<div class="detail-panel-header">' +
                '<div class="detail-panel-title" id="blade-panel-title">叶片详情</div>' +
                '<button class="detail-panel-close" id="blade-panel-close">✕</button>' +
            '</div>' +
            '<div class="detail-panel-body" id="blade-panel-body"></div>' +
        '</div>';
}

function renderBladePanelContent() {
    var title = $('#blade-panel-title');
    var body = $('#blade-panel-body');
    var overlay = $('#panel-overlay');
    var closeBtn = $('#blade-panel-close');

    if (!title || !body) {
        if (state.currentPage === 'turbine-detail') {
            renderTurbineDetail($('#main-content'));
            requestAnimationFrame(function () { renderBladePanelContent(); });
        }
        return;
    }

    title.textContent = '叶片 #' + state.selectedBlade + ' 详情';

    var turbine = state.turbines[state.selectedTurbine - 1] || state.turbines[0];
    var bladeStatuses = turbine.blade_statuses.filter(function (b) { return b.blade_id === state.selectedBlade; });

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

    var fatigue = state.fatigueInfo;
    var damagePct = fatigue ? (fatigue.cumulative_damage * 100) : 0;
    var barClass = damagePct > 75 ? 'purple' : damagePct > 50 ? 'red' : damagePct > 25 ? 'yellow' : 'green';

    var historyHTML = '';
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

    if (closeBtn) {
        closeBtn.addEventListener('click', function () {
            state.bladePanelOpen = false;
            state.selectedBlade = null;
            var panel = $('#blade-panel');
            var overlay = $('#panel-overlay');
            if (panel) panel.remove();
            if (overlay) overlay.remove();
        });
    }
    if (overlay) {
        overlay.addEventListener('click', function () {
            if (closeBtn) closeBtn.click();
        });
    }

    requestAnimationFrame(function () {
        drawHistoryChart(histCanvasId);
    });
}

function drawHistoryChart(canvasId) {
    var canvas = document.getElementById(canvasId);
    if (!canvas) return;

    var rect = canvas.parentElement.getBoundingClientRect();
    var dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    var ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);

    var history = state.cavitationHistory;
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

function renderAlarmsPage(container) {
    var severityFilter = 'all';

    function getFilteredAlarms() {
        if (severityFilter === 'all') return state.alarms;
        return state.alarms.filter(function (a) { return a.severity === severityFilter; });
    }

    function renderTable() {
        var filtered = getFilteredAlarms();
        var rows = filtered.map(function (a) {
            return '<tr>' +
                '<td class="mono">' + formatTime(a.timestamp) + '</td>' +
                '<td>' + a.turbine_id + '号机组</td>' +
                '<td>' + alarmTypeLabel(a.alarm_type) + '</td>' +
                '<td>' + severityBadgeHTML(a.severity) + '</td>' +
                '<td class="mono">叶片#' + a.blade_id + ' 区域' + a.zone_id + '</td>' +
                '<td class="mono">' + a.measured_value.toFixed(3) + '</td>' +
                '<td class="mono">' + a.threshold_value.toFixed(3) + '</td>' +
                '<td>' + (a.iec61850_sent ? '<span style="color:var(--cav-none)">✓ 已推送</span>' : '<span style="color:var(--text-muted)">—</span>') + '</td>' +
            '</tr>';
        }).join('');

        var tableHTML =
            '<table class="data-table">' +
                '<thead><tr>' +
                    '<th>时间</th><th>机组</th><th>告警类型</th><th>级别</th><th>位置</th><th>测量值</th><th>阈值</th><th>IEC 61850</th>' +
                '</tr></thead>' +
                '<tbody>' + rows + '</tbody>' +
            '</table>';

        var tableContainer = $('#alarm-table-container');
        if (tableContainer) tableContainer.innerHTML = tableHTML;
    }

    container.innerHTML =
        '<div class="card" style="margin-bottom:20px">' +
            '<div class="card-header">' +
                '<div class="card-title">告警记录</div>' +
                '<div style="display:flex;gap:8px">' +
                    '<button class="header-btn alarm-filter active" data-severity="all">全部 (' + state.alarms.length + ')</button>' +
                    '<button class="header-btn alarm-filter" data-severity="info">提示</button>' +
                    '<button class="header-btn alarm-filter" data-severity="warning">警告</button>' +
                    '<button class="header-btn alarm-filter" data-severity="critical">紧急</button>' +
                '</div>' +
            '</div>' +
            '<div id="alarm-table-container"></div>' +
        '</div>' +
        '<div class="card">' +
            '<div class="card-header"><div class="card-title">检修建议</div></div>' +
            '<div id="maintenance-advice-container"></div>' +
        '</div>';

    renderTable();

    var criticalAlarms = state.alarms.filter(function (a) {
        return a.severity === 'critical' || a.severity === 'warning';
    });

    var adviceHTML = criticalAlarms.slice(0, 8).map(function (a) {
        return '<div style="padding:12px 0;border-bottom:1px solid rgba(30,58,95,0.3)">' +
            '<div style="display:flex;align-items:center;gap:10px;margin-bottom:6px">' +
                severityBadgeHTML(a.severity) +
                '<span style="font-size:13px;color:var(--text-primary)">' + a.turbine_id + '号机组 · ' + alarmTypeLabel(a.alarm_type) + '</span>' +
                '<span style="font-size:11px;color:var(--text-muted);margin-left:auto">' + formatTime(a.timestamp) + '</span>' +
            '</div>' +
            '<div style="font-size:12px;color:var(--text-secondary);padding-left:2px">' + a.maintenance_advice + '</div>' +
        '</div>';
    }).join('');

    var adviceContainer = $('#maintenance-advice-container');
    if (adviceContainer) adviceContainer.innerHTML = adviceHTML || '<div style="padding:16px;text-align:center;color:var(--text-muted)">暂无检修建议</div>';

    $$('.alarm-filter').forEach(function (btn) {
        btn.addEventListener('click', function () {
            $$('.alarm-filter').forEach(function (b) { b.classList.remove('active'); b.style.borderColor = ''; b.style.color = ''; });
            btn.classList.add('active');
            btn.style.borderColor = 'var(--accent)';
            btn.style.color = 'var(--accent)';
            severityFilter = btn.getAttribute('data-severity');
            renderTable();
        });
    });
}

window._appNavigate = navigate;
window._appInit = init;

document.addEventListener('DOMContentLoaded', init);
