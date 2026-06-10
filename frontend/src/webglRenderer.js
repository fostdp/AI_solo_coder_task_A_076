var VERTEX_SHADER_SRC = [
    '#version 300 es',
    'layout(location = 0) in vec3 aPos;',
    'layout(location = 1) in vec3 aColor;',
    'uniform mat4 uMVP;',
    'out vec3 vColor;',
    'void main() {',
    '    gl_Position = uMVP * vec4(aPos, 1.0);',
    '    vColor = aColor;',
    '}'
].join('\n');

var FRAGMENT_SHADER_SRC = [
    '#version 300 es',
    'precision highp float;',
    'in vec3 vColor;',
    'out vec4 fragColor;',
    'void main() {',
    '    fragColor = vec4(vColor, 0.85);',
    '}'
].join('\n');

function compileShader(gl, type, src) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, src);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
        console.error('Shader compile error:', gl.getShaderInfoLog(shader));
        gl.deleteShader(shader);
        return null;
    }
    return shader;
}

function createProgram(gl, vsSrc, fsSrc) {
    var vs = compileShader(gl, gl.VERTEX_SHADER, vsSrc);
    var fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSrc);
    if (!vs || !fs) return null;

    var prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
        console.error('Program link error:', gl.getProgramInfoLog(prog));
        return null;
    }
    return prog;
}

function mat4Perspective(fov, aspect, near, far) {
    var f = 1.0 / Math.tan(fov / 2);
    var nf = 1 / (near - far);
    return new Float32Array([
        f / aspect, 0, 0, 0,
        0, f, 0, 0,
        0, 0, (far + near) * nf, -1,
        0, 0, 2 * far * near * nf, 0
    ]);
}

function mat4LookAt(eye, center, up) {
    var zx = eye[0] - center[0], zy = eye[1] - center[1], zz = eye[2] - center[2];
    var zl = Math.sqrt(zx * zx + zy * zy + zz * zz);
    zx /= zl; zy /= zl; zz /= zl;

    var xx = up[1] * zz - up[2] * zy;
    var xy = up[2] * zx - up[0] * zz;
    var xz = up[0] * zy - up[1] * zx;
    var xl = Math.sqrt(xx * xx + xy * xy + xz * xz);
    xx /= xl; xy /= xl; xz /= xl;

    var yx = zy * xz - zz * xy;
    var yy = zz * xx - zx * xz;
    var yz = zx * xy - zy * xx;

    return new Float32Array([
        xx, yx, zx, 0,
        xy, yy, zy, 0,
        xz, yz, zz, 0,
        -(xx * eye[0] + xy * eye[1] + xz * eye[2]),
        -(yx * eye[0] + yy * eye[1] + yz * eye[2]),
        -(zx * eye[0] + zy * eye[1] + zz * eye[2]),
        1
    ]);
}

function mat4Multiply(a, b) {
    var out = new Float32Array(16);
    for (var i = 0; i < 4; i++) {
        for (var j = 0; j < 4; j++) {
            out[j * 4 + i] = 0;
            for (var k = 0; k < 4; k++) {
                out[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
            }
        }
    }
    return out;
}

function mat4RotateY(angle) {
    var c = Math.cos(angle), s = Math.sin(angle);
    return new Float32Array([
        c, 0, -s, 0,
        0, 1, 0, 0,
        s, 0, c, 0,
        0, 0, 0, 1
    ]);
}

var MAX_SLICES = 60;
var LOD_NEAR_SLICES = 10;
var LOD_DOWNSAMPLE = 4;
var MAX_GPU_VERTICES = 500000;

function initWaterfallGL(canvas) {
    var gl = canvas.getContext('webgl2', { antialias: true, alpha: true });
    if (!gl) {
        console.error('WebGL2 not supported');
        return null;
    }

    var program = createProgram(gl, VERTEX_SHADER_SRC, FRAGMENT_SHADER_SRC);
    if (!program) return null;

    var vao = gl.createVertexArray();
    var vertexBuffer = gl.createBuffer();

    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer);

    gl.bufferData(gl.ARRAY_BUFFER, MAX_GPU_VERTICES * 24, gl.DYNAMIC_DRAW);

    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 24, 0);

    gl.enableVertexAttribArray(1);
    gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 24, 12);

    gl.bindVertexArray(null);

    return {
        gl: gl,
        program: program,
        vao: vao,
        vertexBuffer: vertexBuffer,
        vertexCount: 0,
        canvas: canvas,
        gpuBufferAllocated: MAX_GPU_VERTICES * 24
    };
}

function updateWaterfallData(glState, waterfallSlices, vertexData) {
    if (!glState) return;

    var gl = glState.gl;

    if (vertexData && vertexData.byteLength > 0) {
        gl.bindBuffer(gl.ARRAY_BUFFER, glState.vertexBuffer);

        if (vertexData.byteLength <= glState.gpuBufferAllocated) {
            gl.bufferSubData(gl.ARRAY_BUFFER, 0, vertexData);
        } else {
            gl.bufferData(gl.ARRAY_BUFFER, vertexData, gl.DYNAMIC_DRAW);
            glState.gpuBufferAllocated = vertexData.byteLength;
        }

        glState.vertexCount = vertexData.byteLength / 24;
    } else {
        glState.vertexCount = 0;
    }
}

function renderWaterfall(glState, rotationAngle) {
    if (!glState || glState.vertexCount === 0) return;

    var gl = glState.gl;
    var canvas = glState.canvas;

    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.043, 0.078, 0.149, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);
    gl.enable(gl.BLEND);
    gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

    var aspect = canvas.width / canvas.height;
    var proj = mat4Perspective(Math.PI / 4, aspect, 0.1, 100);

    var eyeAngle = rotationAngle || 0;
    var eyeDist = 5.5;
    var eye = [
        eyeDist * Math.sin(eyeAngle),
        2.5,
        eyeDist * Math.cos(eyeAngle)
    ];
    var view = mat4LookAt(eye, [0, 0.5, 0], [0, 1, 0]);
    var rotMat = mat4RotateY(0);
    var mv = mat4Multiply(view, rotMat);
    var mvp = mat4Multiply(proj, mv);

    gl.useProgram(glState.program);
    var mvpLoc = gl.getUniformLocation(glState.program, 'uMVP');
    gl.uniformMatrix4fv(mvpLoc, false, mvp);

    gl.bindVertexArray(glState.vao);
    gl.drawArrays(gl.TRIANGLES, 0, glState.vertexCount);
    gl.bindVertexArray(null);
}

function drawWaterfallOverlay(ctx, canvas) {
    ctx.save();
    ctx.font = '11px "JetBrains Mono", monospace';
    ctx.fillStyle = '#8899B4';
    ctx.textAlign = 'center';

    ctx.fillText('频率 (Hz)', canvas.width / 2, canvas.height - 8);
    ctx.save();
    ctx.translate(14, canvas.height / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText('幅值', 0, 0);
    ctx.restore();

    ctx.fillText('时间 →', canvas.width - 40, canvas.height / 2);

    ctx.font = '9px "JetBrains Mono", monospace';
    ctx.fillStyle = '#556688';
    ctx.textAlign = 'left';
    ctx.fillText('0', 30, canvas.height - 18);
    ctx.textAlign = 'right';
    ctx.fillText('25.6kHz', canvas.width - 20, canvas.height - 18);

    ctx.restore();
}

function cleanupWaterfallGL(glState) {
    if (!glState) return;
    var gl = glState.gl;
    gl.deleteBuffer(glState.vertexBuffer);
    gl.deleteProgram(glState.program);
    gl.deleteVertexArray(glState.vao);
}

function createWaterfallWorker() {
    var workerCode = [
        'var MAX_SLICES = 60;',
        'var LOD_NEAR_SLICES = 10;',
        'var LOD_DOWNSAMPLE = 4;',
        '',
        'function spectrumColor(t) {',
        '    t = Math.max(0, Math.min(1, t));',
        '    var stops = [',
        '        [0.0, 10, 10, 80],',
        '        [0.15, 0, 50, 180],',
        '        [0.3, 0, 150, 200],',
        '        [0.45, 0, 200, 100],',
        '        [0.6, 180, 220, 30],',
        '        [0.75, 240, 180, 0],',
        '        [0.9, 240, 60, 30],',
        '        [1.0, 200, 30, 200]',
        '    ];',
        '    for (var i = 0; i < stops.length - 1; i++) {',
        '        if (t >= stops[i][0] && t <= stops[i + 1][0]) {',
        '            var f = (t - stops[i][0]) / (stops[i + 1][0] - stops[i][0]);',
        '            return {',
        '                r: (stops[i][1] + f * (stops[i + 1][1] - stops[i][1])) / 255,',
        '                g: (stops[i][2] + f * (stops[i + 1][2] - stops[i][2])) / 255,',
        '                b: (stops[i][3] + f * (stops[i + 1][3] - stops[i][3])) / 255',
        '            };',
        '        }',
        '    }',
        '    return {r: 0.8, g: 0.12, b: 0.8};',
        '}',
        '',
        'self.onmessage = function(e) {',
        '    var slices = e.data.slices;',
        '    if (!slices || slices.length === 0) {',
        '        self.postMessage({vertexData: null, vertexCount: 0}, []);',
        '        return;',
        '    }',
        '',
        '    while (slices.length > MAX_SLICES) {',
        '        slices.shift();',
        '    }',
        '',
        '    var nSlices = slices.length;',
        '    var baseBins = slices[0].spectrum.length;',
        '    var xScale = 2.0 / baseBins;',
        '    var zScale = 3.0 / nSlices;',
        '    var yScale = 2.0;',
        '',
        '    var vertexArrays = [];',
        '',
        '    for (var s = 0; s < nSlices - 1; s++) {',
        '        var distFromFront = nSlices - 1 - s;',
        '        var step = (distFromFront > LOD_NEAR_SLICES) ? LOD_DOWNSAMPLE : 1;',
        '',
        '        var spec0 = slices[s].spectrum;',
        '        var spec1 = slices[s + 1].spectrum;',
        '        var z0 = (s - nSlices / 2) * zScale;',
        '        var z1 = (s + 1 - nSlices / 2) * zScale;',
        '',
        '        for (var b = 0; b < baseBins - step; b += step) {',
        '            var bNext = Math.min(b + step, baseBins - 1);',
        '            var x0 = (b - baseBins / 2) * xScale;',
        '            var x1 = (bNext - baseBins / 2) * xScale;',
        '',
        '            var v00y = spec0[b] * yScale;',
        '            var v10y = spec0[bNext] * yScale;',
        '            var v01y = spec1[b] * yScale;',
        '            var v11y = spec1[bNext] * yScale;',
        '',
        '            var c00 = spectrumColor(spec0[b]);',
        '            var c10 = spectrumColor(spec0[bNext]);',
        '            var c01 = spectrumColor(spec1[b]);',
        '            var c11 = spectrumColor(spec1[bNext]);',
        '',
        '            vertexArrays.push(',
        '                x0, v00y, z0, c00.r, c00.g, c00.b,',
        '                x1, v10y, z0, c10.r, c10.g, c10.b,',
        '                x0, v01y, z1, c01.r, c01.g, c01.b,',
        '',
        '                x1, v10y, z0, c10.r, c10.g, c10.b,',
        '                x1, v11y, z1, c11.r, c11.g, c11.b,',
        '                x0, v01y, z1, c01.r, c01.g, c01.b',
        '            );',
        '        }',
        '    }',
        '',
        '    var vertexData = new Float32Array(vertexArrays);',
        '    self.postMessage({vertexData: vertexData.buffer, vertexCount: vertexArrays.length / 6}, [vertexData.buffer]);',
        '};'
    ].join('\n');

    var blob = new Blob([workerCode], { type: 'application/javascript' });
    var url = URL.createObjectURL(blob);
    var worker = new Worker(url);
    URL.revokeObjectURL(url);
    return worker;
}

export {
    initWaterfallGL,
    updateWaterfallData,
    renderWaterfall,
    drawWaterfallOverlay,
    cleanupWaterfallGL,
    createWaterfallWorker,
    MAX_SLICES
};
