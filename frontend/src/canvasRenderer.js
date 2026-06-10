import { cavitationStageColor, cavitationIntensityColor, stageToChineseLabel } from './colorScales.js';

var BLADE_COUNT = 13;

function drawTurbineProfile(ctx, width, height, bladeStatuses, selectedBlade, time) {
    ctx.clearRect(0, 0, width, height);

    var cx = width / 2;
    var cy = height * 0.42;
    var runnerRadius = Math.min(width, height) * 0.22;
    var hubRadius = runnerRadius * 0.25;
    var guideRadius = runnerRadius * 1.25;
    var spiralRadius = runnerRadius * 1.7;

    drawDraftTube(ctx, cx, cy, runnerRadius, height);
    drawSpiralCase(ctx, cx, cy, spiralRadius);
    drawGuideVanes(ctx, cx, cy, guideRadius, 20);
    drawRunner(ctx, cx, cy, runnerRadius, hubRadius, bladeStatuses, selectedBlade, time);
    drawComponentLabels(ctx, cx, cy, runnerRadius, spiralRadius, height);
}

function drawSpiralCase(ctx, cx, cy, radius) {
    ctx.save();
    ctx.beginPath();
    ctx.strokeStyle = '#2A4A6F';
    ctx.lineWidth = 3;

    var startAngle = 0;
    var endAngle = Math.PI * 2;
    for (var a = startAngle; a < endAngle; a += 0.02) {
        var r = radius * (1.0 - 0.3 * (a / endAngle));
        var x = cx + r * Math.cos(a);
        var y = cy + r * Math.sin(a);
        if (a === startAngle) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.stroke();

    ctx.fillStyle = 'rgba(17, 29, 53, 0.6)';
    ctx.fill();

    ctx.beginPath();
    ctx.strokeStyle = '#1E3A5F';
    ctx.lineWidth = 1.5;
    ctx.arc(cx, cy, radius * 1.05, 0, Math.PI * 2);
    ctx.stroke();

    var inletX = cx + radius * 1.0;
    var inletY = cy - radius * 0.5;
    ctx.beginPath();
    ctx.fillStyle = '#2A4A6F';
    ctx.moveTo(inletX, inletY - 15);
    ctx.lineTo(inletX + 40, inletY - 20);
    ctx.lineTo(inletX + 40, inletY + 20);
    ctx.lineTo(inletX, inletY + 15);
    ctx.closePath();
    ctx.fill();
    ctx.stroke();

    ctx.restore();
}

function drawGuideVanes(ctx, cx, cy, radius, count) {
    ctx.save();
    for (var i = 0; i < count; i++) {
        var angle = (i / count) * Math.PI * 2 - Math.PI / 2;
        var x = cx + radius * Math.cos(angle);
        var y = cy + radius * Math.sin(angle);

        ctx.save();
        ctx.translate(x, y);
        ctx.rotate(angle + Math.PI / 2);

        ctx.beginPath();
        ctx.fillStyle = '#3A5A7F';
        ctx.strokeStyle = '#4A7AAF';
        ctx.lineWidth = 1;
        ctx.rect(-3, -12, 6, 24);
        ctx.fill();
        ctx.stroke();

        ctx.restore();
    }
    ctx.restore();
}

function drawRunner(ctx, cx, cy, outerRadius, hubRadius, bladeStatuses, selectedBlade, time) {
    ctx.save();

    ctx.beginPath();
    ctx.strokeStyle = '#1E3A5F';
    ctx.lineWidth = 2;
    ctx.arc(cx, cy, outerRadius, 0, Math.PI * 2);
    ctx.stroke();

    ctx.beginPath();
    var gradient = ctx.createRadialGradient(cx, cy, hubRadius, cx, cy, outerRadius);
    gradient.addColorStop(0, 'rgba(22, 32, 64, 0.3)');
    gradient.addColorStop(1, 'rgba(11, 20, 38, 0.6)');
    ctx.fillStyle = gradient;
    ctx.arc(cx, cy, outerRadius, 0, Math.PI * 2);
    ctx.fill();

    var rotationOffset = (time || 0) * 0.0002;

    for (var b = 1; b <= BLADE_COUNT; b++) {
        var baseAngle = ((b - 1) / BLADE_COUNT) * Math.PI * 2 - Math.PI / 2;
        var angle = baseAngle + rotationOffset;

        var bladeStatus = null;
        if (bladeStatuses) {
            for (var s = 0; s < bladeStatuses.length; s++) {
                if (bladeStatuses[s].blade_id === b && bladeStatuses[s].zone_id === 1) {
                    bladeStatus = bladeStatuses[s];
                    break;
                }
            }
        }

        drawBlade(ctx, cx, cy, hubRadius, outerRadius, angle, bladeStatus, b === selectedBlade, time);
    }

    ctx.beginPath();
    var hubGrad = ctx.createRadialGradient(cx, cy, 0, cx, cy, hubRadius);
    hubGrad.addColorStop(0, '#2A4A6F');
    hubGrad.addColorStop(1, '#1E3A5F');
    ctx.fillStyle = hubGrad;
    ctx.arc(cx, cy, hubRadius, 0, Math.PI * 2);
    ctx.fill();

    ctx.beginPath();
    ctx.strokeStyle = '#4A7AAF';
    ctx.lineWidth = 2;
    ctx.arc(cx, cy, hubRadius, 0, Math.PI * 2);
    ctx.stroke();

    ctx.beginPath();
    ctx.fillStyle = '#3A5A7F';
    ctx.arc(cx, cy, hubRadius * 0.3, 0, Math.PI * 2);
    ctx.fill();

    ctx.restore();
}

function drawBlade(ctx, cx, cy, innerR, outerR, angle, status, isSelected, time) {
    ctx.save();

    var bladeWidth = (outerR - innerR) * 0.18;
    var twist = 0.15;

    var cosA = Math.cos(angle);
    var sinA = Math.sin(angle);

    var perpX = -sinA;
    var perpY = cosA;

    var ix = cx + innerR * cosA;
    var iy = cy + innerR * sinA;
    var ox = cx + outerR * cosA;
    var oy = cy + outerR * sinA;

    var midR = (innerR + outerR) * 0.5;
    var mx = cx + midR * cosA;
    var my = cy + midR * sinA;

    var twistX = Math.cos(angle + twist) * bladeWidth * 0.5;
    var twistY = Math.sin(angle + twist) * bladeWidth * 0.5;

    ctx.beginPath();
    ctx.moveTo(ix + perpX * bladeWidth * 0.3, iy + perpY * bladeWidth * 0.3);
    ctx.quadraticCurveTo(
        mx + twistX, my + twistY,
        ox + perpX * bladeWidth * 0.15, oy + perpY * bladeWidth * 0.15
    );
    ctx.lineTo(ox - perpX * bladeWidth * 0.15, oy - perpY * bladeWidth * 0.15);
    ctx.quadraticCurveTo(
        mx - twistX, my - twistY,
        ix - perpX * bladeWidth * 0.3, iy - perpY * bladeWidth * 0.3
    );
    ctx.closePath();

    var bladeColor = '#2A4A6F';
    if (status) {
        bladeColor = cavitationStageColor(status.stage);
    }

    ctx.fillStyle = bladeColor;
    ctx.globalAlpha = 0.7;
    ctx.fill();

    ctx.globalAlpha = 1.0;
    ctx.strokeStyle = isSelected ? '#00D4FF' : '#3A6A9F';
    ctx.lineWidth = isSelected ? 3 : 1.5;
    ctx.stroke();

    if (isSelected) {
        ctx.save();
        ctx.shadowColor = '#00D4FF';
        ctx.shadowBlur = 15;
        ctx.strokeStyle = 'rgba(0, 212, 255, 0.5)';
        ctx.lineWidth = 2;
        ctx.stroke();
        ctx.restore();
    }

    if (status && status.intensity > 0.1) {
        drawCavitationCloud(ctx, mx, my, (outerR - innerR) * 0.4, status.intensity, status.stage, time);
    }

    ctx.restore();
}

function drawCavitationCloud(ctx, centerX, centerY, radius, intensity, stage, time) {
    ctx.save();

    var color = cavitationIntensityColor(intensity);
    var pulseAlpha = 0.3 + 0.15 * Math.sin((time || 0) * 0.003 + intensity * 10);

    var grad = ctx.createRadialGradient(centerX, centerY, 0, centerX, centerY, radius);
    grad.addColorStop(0, 'rgba(' + color.r + ',' + color.g + ',' + color.b + ',' + pulseAlpha + ')');
    grad.addColorStop(0.6, 'rgba(' + color.r + ',' + color.g + ',' + color.b + ',' + (pulseAlpha * 0.5) + ')');
    grad.addColorStop(1, 'rgba(' + color.r + ',' + color.g + ',' + color.b + ',0)');

    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.arc(centerX, centerY, radius, 0, Math.PI * 2);
    ctx.fill();

    if (stage === 'developed' || stage === 'critical') {
        ctx.beginPath();
        ctx.strokeStyle = 'rgba(' + color.r + ',' + color.g + ',' + color.b + ',0.4)';
        ctx.lineWidth = 1;
        ctx.arc(centerX, centerY, radius * 1.2, 0, Math.PI * 2);
        ctx.stroke();
    }

    ctx.restore();
}

function drawDraftTube(ctx, cx, cy, runnerRadius, canvasHeight) {
    ctx.save();

    var topY = cy + runnerRadius * 0.9;
    var topWidth = runnerRadius * 0.8;
    var bottomY = canvasHeight - 40;
    var bottomWidth = runnerRadius * 1.6;

    ctx.beginPath();
    ctx.moveTo(cx - topWidth, topY);
    ctx.lineTo(cx - bottomWidth, bottomY);
    ctx.lineTo(cx + bottomWidth, bottomY);
    ctx.lineTo(cx + topWidth, topY);
    ctx.closePath();

    var grad = ctx.createLinearGradient(cx, topY, cx, bottomY);
    grad.addColorStop(0, 'rgba(17, 29, 53, 0.7)');
    grad.addColorStop(1, 'rgba(11, 20, 38, 0.9)');
    ctx.fillStyle = grad;
    ctx.fill();

    ctx.strokeStyle = '#2A4A6F';
    ctx.lineWidth = 2;
    ctx.stroke();

    ctx.restore();
}

function drawComponentLabels(ctx, cx, cy, runnerRadius, spiralRadius, canvasHeight) {
    ctx.save();
    ctx.font = '13px "Noto Sans SC", sans-serif';
    ctx.fillStyle = '#8899B4';
    ctx.textAlign = 'center';

    ctx.fillText('蜗壳', cx + spiralRadius * 0.7, cy - spiralRadius * 0.3);

    ctx.fillText('导叶', cx + runnerRadius * 1.45, cy + 5);

    ctx.fillText('转轮', cx, cy + 5);

    var draftY = cy + runnerRadius + (canvasHeight - cy - runnerRadius) * 0.3;
    ctx.fillText('尾水管', cx, draftY);

    ctx.restore();
}

function drawSensorMarkers(ctx, cx, cy, runnerRadius, spiralRadius) {
    ctx.save();

    var hydrophonePositions = [
        { x: cx + spiralRadius * 0.8, y: cy - spiralRadius * 0.2, label: 'H1' },
        { x: cx - spiralRadius * 0.3, y: cy - spiralRadius * 0.7, label: 'H2' },
        { x: cx - spiralRadius * 0.1, y: cy + runnerRadius * 1.5, label: 'H3' },
        { x: cx + runnerRadius * 0.5, y: cy - runnerRadius * 0.3, label: 'H4' },
        { x: cx - runnerRadius * 0.4, y: cy + runnerRadius * 0.2, label: 'H7' },
    ];

    var accelPositions = [
        { x: cx + spiralRadius * 0.6, y: cy + spiralRadius * 0.3, label: 'A1' },
        { x: cx - spiralRadius * 0.5, y: cy - spiralRadius * 0.4, label: 'A2' },
        { x: cx + runnerRadius * 0.2, y: cy + runnerRadius * 1.3, label: 'A3' },
    ];

    ctx.fillStyle = '#00D4FF';
    for (var i = 0; i < hydrophonePositions.length; i++) {
        var p = hydrophonePositions[i];
        ctx.beginPath();
        ctx.arc(p.x, p.y, 4, 0, Math.PI * 2);
        ctx.fill();
        ctx.font = '9px "JetBrains Mono", monospace';
        ctx.fillText(p.label, p.x, p.y - 8);
    }

    ctx.fillStyle = '#F39C12';
    for (var j = 0; j < accelPositions.length; j++) {
        var ap = accelPositions[j];
        ctx.fillRect(ap.x - 3, ap.y - 3, 6, 6);
        ctx.font = '9px "JetBrains Mono", monospace';
        ctx.fillText(ap.label, ap.x, ap.y - 8);
    }

    ctx.restore();
}

function hitTestBlade(x, y, centerX, centerY, radius, bladeCount) {
    var dx = x - centerX;
    var dy = y - centerY;
    var dist = Math.sqrt(dx * dx + dy * dy);
    var hubRadius = radius * 0.25;

    if (dist < hubRadius || dist > radius) return 0;

    var angle = Math.atan2(dy, dx) + Math.PI / 2;
    if (angle < 0) angle += Math.PI * 2;

    var bladeAngle = Math.PI * 2 / bladeCount;
    var bladeId = Math.floor(angle / bladeAngle) + 1;

    if (bladeId > bladeCount) bladeId = bladeCount;
    return bladeId;
}

function drawBladeInfo(ctx, x, y, bladeId, status, fatigue) {
    ctx.save();

    var boxW = 180;
    var boxH = 90;
    var bx = x + 15;
    var by = y - boxH / 2;

    if (bx + boxW > ctx.canvas.width) bx = x - boxW - 15;

    ctx.fillStyle = 'rgba(22, 32, 64, 0.95)';
    ctx.strokeStyle = '#1E3A5F';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.roundRect(bx, by, boxW, boxH, 6);
    ctx.fill();
    ctx.stroke();

    ctx.font = '12px "Noto Sans SC", sans-serif';
    ctx.fillStyle = '#E8EDF5';
    ctx.textAlign = 'left';
    ctx.fillText('叶片 #' + bladeId, bx + 10, by + 20);

    if (status) {
        var stageColor = cavitationStageColor(status.stage);
        ctx.fillStyle = stageColor;
        ctx.fillText(stageToChineseLabel(status.stage), bx + 70, by + 20);

        ctx.fillStyle = '#8899B4';
        ctx.font = '11px "JetBrains Mono", monospace';
        ctx.fillText('强度: ' + (status.intensity * 100).toFixed(1) + '%', bx + 10, by + 40);
        ctx.fillText('异常分: ' + status.anomaly_score.toFixed(3), bx + 10, by + 56);
    }

    if (fatigue) {
        ctx.fillStyle = '#8899B4';
        ctx.font = '11px "JetBrains Mono", monospace';
        ctx.fillText('累积损伤: ' + (fatigue.cumulative_damage * 100).toFixed(2) + '%', bx + 10, by + 72);
    }

    ctx.restore();
}

export {
    drawTurbineProfile,
    drawCavitationCloud,
    drawSensorMarkers,
    drawBladeInfo,
    hitTestBlade,
    BLADE_COUNT
};
