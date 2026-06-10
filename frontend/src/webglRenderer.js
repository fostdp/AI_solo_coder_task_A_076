import { spectrumColor } from './colorScales.js';

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
        canvas: canvas
    };
}

function updateWaterfallData(glState, waterfallSlices) {
    if (!glState || !waterfallSlices || waterfallSlices.length === 0) return;

    var gl = glState.gl;
    var slices = waterfallSlices;
    var nSlices = slices.length;
    var nBins = slices[0].spectrum.length;
    var xScale = 2.0 / nBins;
    var zScale = 3.0 / nSlices;
    var yScale = 2.0;

    var vertices = [];

    for (var s = 0; s < nSlices - 1; s++) {
        var spec0 = slices[s].spectrum;
        var spec1 = slices[s + 1].spectrum;
        var z0 = (s - nSlices / 2) * zScale;
        var z1 = (s + 1 - nSlices / 2) * zScale;

        for (var b = 0; b < nBins - 1; b++) {
            var x0 = (b - nBins / 2) * xScale;
            var x1 = (b + 1 - nBins / 2) * xScale;

            var v00y = spec0[b] * yScale;
            var v10y = spec0[b + 1] * yScale;
            var v01y = spec1[b] * yScale;
            var v11y = spec1[b + 1] * yScale;

            var c00 = spectrumColor(spec0[b]);
            var c10 = spectrumColor(spec0[b + 1]);
            var c01 = spectrumColor(spec1[b]);
            var c11 = spectrumColor(spec1[b + 1]);

            vertices.push(
                x0, v00y, z0, c00.r / 255, c00.g / 255, c00.b / 255,
                x1, v10y, z0, c10.r / 255, c10.g / 255, c10.b / 255,
                x0, v01y, z1, c01.r / 255, c01.g / 255, c01.b / 255,

                x1, v10y, z0, c10.r / 255, c10.g / 255, c10.b / 255,
                x1, v11y, z1, c11.r / 255, c11.g / 255, c11.b / 255,
                x0, v01y, z1, c01.r / 255, c01.g / 255, c01.b / 255
            );
        }
    }

    var vertexData = new Float32Array(vertices);
    gl.bindBuffer(gl.ARRAY_BUFFER, glState.vertexBuffer);
    gl.bufferData(gl.ARRAY_BUFFER, vertexData, gl.DYNAMIC_DRAW);

    glState.vertexCount = vertices.length / 6;
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

export {
    initWaterfallGL,
    updateWaterfallData,
    renderWaterfall,
    drawWaterfallOverlay,
    cleanupWaterfallGL
};
