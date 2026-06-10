import { cavitationStageColor, alarmSeverityColor, stageToChineseLabel } from './colorScales.js';
import { Turbine3DViewer, BLADE_COUNT } from './turbine_3d_viewer.js';
import { BladeDetailPanel, formatDuration, stageBadgeHTML, severityBadgeHTML } from './blade_detail.js';
import { initWaterfallGL, updateWaterfallData, renderWaterfall, drawWaterfallOverlay, cleanupWaterfallGL, createWaterfallWorker, MAX_SLICES } from './webglRenderer.js';
import { generateTurbineStatuses, generateSpectrumData, generateWaterfallData, generateCavitationHistory, generateAlarms, generateFatigueInfo } from './mockData.js';

var state = {
    currentPage: 'dashboard',
    turbines: [],
    selectedTurbine: null,
    selectedBlade: null,
    bladePanelOpen: false,
    turbineViewer: null,
    bladePanel: null,
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

    state.turbineViewer = new Turbine3DViewer(canvas);

    function animate(time) {
        state.turbineViewer.render(turbine.blade_statuses, state.selectedBlade, time);
        state.animFrame = requestAnimationFrame(animate);
    }

    canvas.addEventListener('click', function (e) {
        var canvasRect = canvas.getBoundingClientRect();
        var x = e.clientX - canvasRect.left;
        var y = e.clientY - canvasRect.top;
        var bladeId = state.turbineViewer.hitTest(x, y);
        if (bladeId > 0) {
            state.selectedBlade = bladeId;
            state.bladePanelOpen = true;
            state.cavitationHistory = generateCavitationHistory(24);
            state.fatigueInfo = generateFatigueInfo();
            showBladePanel(turbine);
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

function showBladePanel(turbine) {
    if (!state.bladePanel) {
        state.bladePanel = new BladeDetailPanel($('#main-content'));
    }
    var bladeStatuses = turbine.blade_statuses.filter(function (b) { return b.blade_id === state.selectedBlade; });
    state.bladePanel.show(state.selectedBlade, bladeStatuses, state.fatigueInfo, state.cavitationHistory);
}

function renderBladePanel() {
    return '';
}

function renderBladePanelContent() {
    var turbine = state.turbines[state.selectedTurbine - 1] || state.turbines[0];
    showBladePanel(turbine);
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
