// DEMIURGE — js/render/gl.js : DM.GL, the WebGL2 helper every render module is built on (SPEC-RENDER §1–3).
//
//   GL.init(canvas) → bool         context + capabilities + the Frame uniform buffer. Callable again after a context restore.
//   GL.gl, GL.canvas, GL.caps = { hdr, floatLinear, maxSamples, maxTex }
//   GL.ATTR = { aPos: 0, aNormal: 1, aColor: 2, aData: 3, aI0: 4, aI1: 5, aI2: 6, aI3: 7 }
//   GL.CLOUD_R = DM.R + 11, GL.ATMO_R = DM.R + 17
//   GL.quality (0 | 1 | 2), GL.view (the Camera.view() fields + viewProj; null = far orbit). Both are written by the renderer.
//   GL.chunk(name, glsl)           register a shader chunk; `#include <name>` on its own line pulls it in (once per shader)
//   GL.program(name, vsBody, fsBody) → prog { id, name, u.<uniform>, use() }
//        Bodies are GLSL ES 3.00 without #version / precision lines; the helper prepends them and the Frame block, expands
//        includes, pins the GL.ATTR attribute locations, binds Frame to binding point 0 and `uShadowMap` to texture unit 7.
//        Compilation starts at once; the link is checked on the first prog.use() (or first read of prog.u), which throws an
//        Error with the program name, the driver log and the numbered source. Same name + same source returns the cached program.
//   GL.mesh({ aPos, aNormal, aColor, aData, aI0..aI3, index, dynamic = [] }) → mesh
//        mesh.vao, mesh.count, mesh.update(name, Float32Array), mesh.setIndex(Uint16Array | Uint32Array),
//        mesh.draw(mode = TRIANGLES, count = mesh.count, first = 0), mesh.free()
//   GL.instances(mesh, capacity) → inst
//        inst.data (16 floats per instance: aI0 aI1 aI2 aI3), inst.count, inst.capacity, inst.upload(first = 0, count = inst.count),
//        inst.draw(count = inst.count), inst.ensure(capacity), inst.free()
//   GL.target(w, h, { hdr, depth, depthTex, samples, linear }) → target
//        target.fbo, .tex, .depthTex, .w, .h, .samples, .hdr, bind(), resize(w, h), blitTo(dst), free()
//   GL.depthTarget(size) → shadow-map target (only target.depthTex, hardware compare LEQUAL, LINEAR, CLAMP_TO_EDGE)
//   GL.bindTex(unit, texture)      binds a 2D texture on a unit and leaves unit 0 active
//   GL.toScreen()                  bind the canvas, full viewport
//   GL.fsTri()                     draw one fullscreen triangle (use with GL.FS_VS, which provides `out vec2 vUv;`)
//   GL.frame                       CPU mirror of the Frame block: set(name, x, y, z, w), setComp(name, i, v), setMat(name, m4),
//                                  setStorm(i, x, y, z, radius), upload(); also get(name) → live Float32Array view, data, offsets
//   GL.checkErrors(label)          logs gl.getError() when GL.debug is true
//   Extras: GL.resetState() (the canonical GL state of SPEC-RENDER §4), GL.stats = { draws, tris, instances } (counted by
//   the draw helpers, reset by the renderer each frame), GL.FRAME_GLSL, GL.force = { noHdr } (dev: pretend there are no float targets).
//
// Shader chunks registered here (SPEC-RENDER §3; nobody else edits them):
//   <noise>     float hash13(vec3)  vec3 hash33(vec3)  float vnoise(vec3)  float fbm3(vec3)  float fbm5(vec3)       all 0..1
//   <clouds>    float cloudCover(vec3 dir)                         (includes <noise>)
//   <lighting>  float dayAt(vec3 worldPos)                         (includes <clouds>; declares `uniform sampler2DShadow uShadowMap;`)
//               float sunShadow(vec3 worldPos, vec3 normal)
//               float cloudShade(vec3 worldPos)
//               vec3  lightAt(vec3 worldPos)                       multiply by the particle / cloud albedo
//               vec3  shade(vec3 albedo, vec3 normal, vec3 worldPos, float specular, float emissive)
//               vec3  inscatter(vec3 rayOrigin, vec3 rayDir, float tMax)
//               vec3  atmosphere(vec3 color, vec3 worldPos)
//               vec3  moonDir()                                    extra: unit vector toward the moon that lights the night side
//   The chunks use no derivatives and no gl_FragCoord, so they also work in vertex shaders. Every helper they define beyond the
//   names above starts with `dm_` / `DM_`: do not use those prefixes in your own shaders.
//
// Lighting calibration (SPEC-RENDER §0): a white surface facing the noon sun returns 2.5 (uSunCol + sky fill); the brightest
// diffuse value (albedo 0.85) is about 2.15, below the bloom knee. The night side gets about 0.4 of cool moon + sky light.
//
// Rules the helpers keep: they bind their own VAO for every draw and index-buffer operation and unbind it afterwards; disabled
// attributes are set to constants at every draw (normal 0,1,0; colour 1,1,1,1; the rest 0); only target.bind() and GL.toScreen()
// change the framebuffer and viewport (target creation, resize and blitTo restore the previous binding); texture unit 0 is
// left active and unbound by target creation.
(function (DM) {
  'use strict';

  const GL = (DM.GL = DM.GL || {});
  const R0 = DM.R || 100;
  let gl = null;
  let gen = 0;                       // bumped by every GL.init: objects of an older generation are dead
  let ubo = null, fsVao = null;
  let boundFbo = null, boundW = 0, boundH = 0;      // what target.bind() / toScreen() bound last (null = canvas)

  GL.gl = null;
  GL.canvas = null;
  GL.caps = { hdr: false, floatLinear: false, maxSamples: 0, maxTex: 0 };
  GL.ATTR = { aPos: 0, aNormal: 1, aColor: 2, aData: 3, aI0: 4, aI1: 5, aI2: 6, aI3: 7 };
  GL.CLOUD_R = R0 + 11;
  GL.ATMO_R = R0 + 17;
  GL.quality = GL.quality === undefined ? 2 : GL.quality;
  GL.view = null;
  GL.debug = false;
  GL.force = GL.force || { noHdr: false };
  GL.stats = { draws: 0, tris: 0, instances: 0 };

  const ATTR_NAMES = ['aPos', 'aNormal', 'aColor', 'aData', 'aI0', 'aI1', 'aI2', 'aI3'];
  const ATTR_SIZE = [3, 3, 4, 4, 4, 4, 4, 4];
  const ATTR_CONST = [[0, 0, 0, 0], [0, 1, 0, 0], [1, 1, 1, 1], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];

  // ------------------------------------------------------------------------------------------ Frame block
  GL.FRAME_GLSL = [
    'layout(std140) uniform Frame {',
    '  mat4 uViewProj;      // world -> clip (the light matrix during the shadow pass)',
    '  mat4 uView;          // world -> eye',
    '  mat4 uInvViewProj;   // clip -> world (camera)',
    '  mat4 uShadowMat;     // world -> shadow-map texture space (xyz in 0..1)',
    '  vec4 uCamPos;        // xyz eye position, w height of the eye above the datum sphere',
    '  vec4 uSunDir;        // xyz unit vector toward the sun, w light scale (1; 0.4 without HDR targets)',
    '  vec4 uSunCol;        // rgb sun colour, w ambient strength',
    '  vec4 uTime;          // x real seconds, y sim year, z cloud drift time, w frame dt',
    '  vec4 uScreen;        // x,y framebuffer size in px, z zoom01 (0 far .. 1 near), w quality 0|1|2',
    '  vec4 uPlanet;        // x DM.R, y displayed sea level, z GL.CLOUD_R, w GL.ATMO_R',
    '  vec4 uBrush;         // xyz unit direction of the brush centre, w angular radius (0 = hidden)',
    '  vec4 uBrushCol;      // rgb, a (a < 0.5 = draw the ring dashed: invalid target)',
    '  vec4 uFlash;         // xyz world position of a transient light, w radius (0 = none)',
    '  vec4 uFlashCol;      // rgb intensity of that light',
    '  vec4 uWorld;         // x heat (1 molten .. 0 cooled), y dust 0..1, z night-light level 0..1, w overlay mode',
    '  vec4 uFx;            // x cloud opacity (fades as the camera descends), y cloudiness 0..1, z ocean fraction 0..1, w free',
    '  vec4 uStorms[8];     // xyz unit direction, w angular radius (0 = unused)',
    '};',
  ].join('\n');

  const FRAME_FIELDS = [
    ['uViewProj', 16], ['uView', 16], ['uInvViewProj', 16], ['uShadowMat', 16],
    ['uCamPos', 4], ['uSunDir', 4], ['uSunCol', 4], ['uTime', 4], ['uScreen', 4], ['uPlanet', 4],
    ['uBrush', 4], ['uBrushCol', 4], ['uFlash', 4], ['uFlashCol', 4], ['uWorld', 4], ['uFx', 4], ['uStorms', 32],
  ];
  const frameOff = Object.create(null), frameLen = Object.create(null);
  let frameFloats = 0;
  for (let i = 0; i < FRAME_FIELDS.length; i++) {
    frameOff[FRAME_FIELDS[i][0]] = frameFloats;
    frameLen[FRAME_FIELDS[i][0]] = FRAME_FIELDS[i][1];
    frameFloats += FRAME_FIELDS[i][1];
  }
  const frameData = new Float32Array(frameFloats);          // 144 floats = 576 bytes, std140 with no padding
  function fieldOff(name) {
    const o = frameOff[name];
    if (o === undefined) throw new Error('GL.frame: unknown field "' + name + '"');
    return o;
  }
  GL.frame = {
    data: frameData,
    size: frameFloats * 4,
    names: FRAME_FIELDS.map(f => f[0]),
    offsets: frameOff,                                       // field name → index of its first float (byte offset = 4 × this)
    set(name, x, y, z, w) {
      const o = fieldOff(name);
      frameData[o] = x || 0; frameData[o + 1] = y || 0; frameData[o + 2] = z || 0; frameData[o + 3] = w || 0;
    },
    setComp(name, index, value) { frameData[fieldOff(name) + (index | 0)] = value; },
    setMat(name, m) { frameData.set(m.length === 16 ? m : Array.prototype.slice.call(m, 0, 16), fieldOff(name)); },
    setStorm(i, x, y, z, radius) {
      if (!(i >= 0 && i < 8)) return;
      const o = frameOff.uStorms + 4 * (i | 0);
      frameData[o] = x || 0; frameData[o + 1] = y || 0; frameData[o + 2] = z || 0; frameData[o + 3] = radius || 0;
    },
    get(name) { const o = fieldOff(name); return frameData.subarray(o, o + frameLen[name]); },
    upload() {
      if (!gl || !ubo) return;
      gl.bindBuffer(gl.UNIFORM_BUFFER, ubo);
      gl.bufferSubData(gl.UNIFORM_BUFFER, 0, frameData);
      gl.bindBuffer(gl.UNIFORM_BUFFER, null);
    },
  };

  // ------------------------------------------------------------------------------------------ init
  GL.init = function (canvas) {
    let ctx = null;
    try {
      ctx = canvas.getContext('webgl2', { antialias: false, alpha: false, depth: false, stencil: false,
        powerPreference: 'high-performance', preserveDrawingBuffer: false });
    } catch (e) { ctx = null; }
    if (!ctx) { gl = GL.gl = null; return false; }
    gl = GL.gl = ctx;
    GL.canvas = canvas;
    gen++;
    programs.clear();
    boundFbo = null; boundW = canvas.width; boundH = canvas.height;

    // capabilities
    const caps = GL.caps;
    let hdr = !!gl.getExtension('EXT_color_buffer_float');
    if (!hdr) hdr = !!gl.getExtension('EXT_color_buffer_half_float');
    if (hdr) {                                               // trust, but verify with a real framebuffer
      const tex = gl.createTexture();
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA16F, 4, 4);
      const fb = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      hdr = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.bindTexture(gl.TEXTURE_2D, null);
      gl.deleteFramebuffer(fb);
      gl.deleteTexture(tex);
    }
    if (GL.force && GL.force.noHdr) hdr = false;
    caps.hdr = hdr;
    caps.floatLinear = !!gl.getExtension('OES_texture_float_linear');
    let most = 0;
    try {                                                    // (without float targets the scene is RGB10_A2 or RGBA8: both allow as many)
      const list = gl.getInternalformatParameter(gl.RENDERBUFFER, hdr ? gl.RGBA16F : gl.RGBA8, gl.SAMPLES);
      if (list) for (let i = 0; i < list.length; i++) if (list[i] > most) most = list[i];
    } catch (e) { most = 0; }
    caps.maxSamples = Math.min(4, most);
    caps.maxTex = gl.getParameter(gl.MAX_TEXTURE_SIZE) | 0;

    // the Frame uniform buffer lives on binding point 0 for the whole session
    ubo = gl.createBuffer();
    gl.bindBuffer(gl.UNIFORM_BUFFER, ubo);
    gl.bufferData(gl.UNIFORM_BUFFER, frameData, gl.DYNAMIC_DRAW);
    gl.bindBuffer(gl.UNIFORM_BUFFER, null);
    gl.bindBufferBase(gl.UNIFORM_BUFFER, 0, ubo);

    fsVao = gl.createVertexArray();                          // no attributes: GL.FS_VS works from gl_VertexID
    GL.resetState();
    return true;
  };

  /** The canonical state every draw function may assume on entry and must restore on exit (SPEC-RENDER §4). */
  GL.resetState = function () {
    if (!gl) return;
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LEQUAL);
    gl.depthMask(true);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    gl.cullFace(gl.BACK);
    gl.frontFace(gl.CCW);
    gl.disable(gl.SCISSOR_TEST);
    gl.bindVertexArray(null);
    gl.activeTexture(gl.TEXTURE0);
  };

  GL.checkErrors = function (label) {
    if (!GL.debug || !gl) return 0;
    let first = 0;
    for (let i = 0; i < 8; i++) {
      const e = gl.getError();
      if (e === gl.NO_ERROR) break;
      if (!first) first = e;
      console.error('DM.GL: WebGL error 0x' + e.toString(16) + ' at ' + label);
    }
    return first;
  };

  // ------------------------------------------------------------------------------------------ shader chunks and programs
  const chunks = Object.create(null);
  const programs = new Map();

  GL.chunk = function (name, glsl) { chunks[name] = String(glsl).replace(/\r\n?/g, '\n'); };

  function expand(src, seen, progName) {
    return src.replace(/^[ \t]*#include[ \t]+<([\w.\-]+)>[ \t]*$/gm, function (line, name) {
      if (seen[name]) return '';
      if (!(name in chunks)) throw new Error('GL.program "' + progName + '": unknown shader chunk <' + name + '>');
      seen[name] = true;
      return expand(chunks[name], seen, progName);
    });
  }

  const HEADER = '#version 300 es\n' +
    'precision highp float; precision highp int; precision highp sampler2D; precision highp samplerCube; precision highp sampler2DShadow;\n' +
    GL.FRAME_GLSL + '\n';

  function numbered(src) {
    const lines = src.split('\n');
    const out = new Array(lines.length);
    for (let i = 0; i < lines.length; i++) out[i] = ('    ' + (i + 1)).slice(-4) + ': ' + lines[i];
    return out.join('\n');
  }

  function finish(p) {
    if (p._ready) return;
    if (p._err) throw new Error(p._err);
    if (p._gen !== gen) throw new Error('GL.program "' + p.name + '": created before the last GL.init (dead context); create it again in init()');
    if (gl.isContextLost()) return;
    if (!gl.getProgramParameter(p.id, gl.LINK_STATUS)) {
      let msg = 'GL.program "' + p.name + '" failed to build.';
      let shaderFailed = false;
      const parts = [['vertex', p._vs, p._vsSrc], ['fragment', p._fs, p._fsSrc]];
      for (let i = 0; i < 2; i++) {
        if (gl.getShaderParameter(parts[i][1], gl.COMPILE_STATUS)) continue;
        shaderFailed = true;
        msg += '\n--- ' + parts[i][0] + ' shader log:\n' + (gl.getShaderInfoLog(parts[i][1]) || '(empty)').trim() +
          '\n--- ' + parts[i][0] + ' shader source:\n' + numbered(parts[i][2]);
      }
      msg += '\n--- program log:\n' + (gl.getProgramInfoLog(p.id) || '(empty)').trim();
      if (!shaderFailed) msg += '\n--- vertex shader source:\n' + numbered(p._vsSrc) + '\n--- fragment shader source:\n' + numbered(p._fsSrc);
      p._err = msg;
      throw new Error(msg);
    }
    const u = p._u;
    const n = gl.getProgramParameter(p.id, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) {
      const info = gl.getActiveUniform(p.id, i);
      if (!info) continue;
      const loc = gl.getUniformLocation(p.id, info.name);
      if (!loc) continue;                                    // members of the Frame block have no location
      u[info.name] = loc;
      if (info.name.slice(-3) === '[0]') u[info.name.slice(0, -3)] = loc;
    }
    const block = gl.getUniformBlockIndex(p.id, 'Frame');
    if (block !== gl.INVALID_INDEX) gl.uniformBlockBinding(p.id, block, 0);
    if (u.uShadowMap) {
      const prev = gl.getParameter(gl.CURRENT_PROGRAM);
      gl.useProgram(p.id);
      gl.uniform1i(u.uShadowMap, 7);
      gl.useProgram(prev);
    }
    gl.detachShader(p.id, p._vs); gl.detachShader(p.id, p._fs);
    gl.deleteShader(p._vs); gl.deleteShader(p._fs);
    p._vs = p._fs = null;
    p._ready = true;
  }

  GL.program = function (name, vsBody, fsBody) {
    if (!gl) throw new Error('GL.program "' + name + '": call GL.init first');
    const vsSrc = HEADER + expand(String(vsBody).replace(/\r\n?/g, '\n'), Object.create(null), name);
    const fsSrc = HEADER + expand(String(fsBody).replace(/\r\n?/g, '\n'), Object.create(null), name);
    const old = programs.get(name);
    if (old && old._gen === gen && old._vsSrc === vsSrc && old._fsSrc === fsSrc) return old;
    const id = gl.createProgram();
    const vs = gl.createShader(gl.VERTEX_SHADER), fs = gl.createShader(gl.FRAGMENT_SHADER);
    gl.shaderSource(vs, vsSrc); gl.compileShader(vs);
    gl.shaderSource(fs, fsSrc); gl.compileShader(fs);
    gl.attachShader(id, vs); gl.attachShader(id, fs);
    for (let i = 0; i < ATTR_NAMES.length; i++) gl.bindAttribLocation(id, i, ATTR_NAMES[i]);
    gl.linkProgram(id);                                      // status is read on first use, so programs compile in parallel
    const p = {
      id, name, _gen: gen, _vs: vs, _fs: fs, _vsSrc: vsSrc, _fsSrc: fsSrc, _u: {}, _ready: false, _err: null,
      use() { if (!this._ready) finish(this); gl.useProgram(this.id); return this; },
    };
    Object.defineProperty(p, 'u', { enumerable: true, get() { if (!this._ready) finish(this); return this._u; } });
    programs.set(name, p);
    return p;
  };

  // ------------------------------------------------------------------------------------------ meshes
  function f32(a) { return a instanceof Float32Array ? a : new Float32Array(a); }
  function setConstants(present, upTo) {
    for (let i = 0; i < upTo; i++) {
      if (present[i]) continue;
      const c = ATTR_CONST[i];
      gl.vertexAttrib4f(i, c[0], c[1], c[2], c[3]);
    }
  }
  function countTris(mode, count) {
    if (mode === gl.TRIANGLES) return (count / 3) | 0;
    if (mode === gl.TRIANGLE_STRIP || mode === gl.TRIANGLE_FAN) return count > 2 ? count - 2 : 0;
    return 0;
  }

  GL.mesh = function (spec) {
    if (!gl) throw new Error('GL.mesh: call GL.init first');
    spec = spec || {};
    const dyn = spec.dynamic || [];
    const m = {
      vao: gl.createVertexArray(), count: 0, indexed: false,
      _bufs: new Array(8).fill(null), _bytes: new Array(8).fill(0), _present: new Array(8).fill(false),
      _ibo: null, _ibytes: 0, _itype: 0, _isize: 2, _ver: 0, _verts: 0, _dyn: dyn, _gen: gen,
    };

    function attach(i, data) {                               // first data for attribute i: create its buffer, wire it into the VAO
      const buf = gl.createBuffer();
      gl.bindVertexArray(m.vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, data, dyn.indexOf(ATTR_NAMES[i]) >= 0 ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
      gl.enableVertexAttribArray(i);
      gl.vertexAttribPointer(i, ATTR_SIZE[i], gl.FLOAT, false, 0, 0);
      gl.bindVertexArray(null);
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
      m._bufs[i] = buf; m._bytes[i] = data.byteLength; m._present[i] = true; m._ver++;
    }

    m.update = function (name, array) {
      const i = GL.ATTR[name];
      if (i === undefined) throw new Error('GL.mesh.update: unknown attribute "' + name + '"');
      const data = f32(array);
      if (!m._bufs[i]) attach(i, data);
      else {
        gl.bindBuffer(gl.ARRAY_BUFFER, m._bufs[i]);
        if (data.byteLength > m._bytes[i]) {
          gl.bufferData(gl.ARRAY_BUFFER, data, dyn.indexOf(name) >= 0 ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
          m._bytes[i] = data.byteLength;
        } else if (data.byteLength > 0) gl.bufferSubData(gl.ARRAY_BUFFER, 0, data);
        gl.bindBuffer(gl.ARRAY_BUFFER, null);
      }
      if (i === 0) { m._verts = (data.length / 3) | 0; if (!m.indexed) m.count = m._verts; }
      return m;
    };

    m.setIndex = function (array) {
      if (!array) return m;
      if (!(array instanceof Uint16Array) && !(array instanceof Uint32Array)) array = new Uint32Array(array);
      gl.bindVertexArray(m.vao);                             // the element-array binding is VAO state
      if (!m._ibo) {
        m._ibo = gl.createBuffer();
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, m._ibo);
        m._ibytes = 0; m._ver++;
      }
      if (array.byteLength > m._ibytes) {
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, array, dyn.indexOf('index') >= 0 ? gl.DYNAMIC_DRAW : gl.STATIC_DRAW);
        m._ibytes = array.byteLength;
      } else if (array.byteLength > 0) gl.bufferSubData(gl.ELEMENT_ARRAY_BUFFER, 0, array);
      gl.bindVertexArray(null);
      m._itype = array instanceof Uint16Array ? gl.UNSIGNED_SHORT : gl.UNSIGNED_INT;
      m._isize = array instanceof Uint16Array ? 2 : 4;
      m.indexed = true;
      m.count = array.length;
      return m;
    };

    m.draw = function (mode, count, first) {
      if (mode === undefined) mode = gl.TRIANGLES;
      if (count === undefined) count = m.count;
      first = first || 0;
      if (!(count > 0)) return;
      gl.bindVertexArray(m.vao);
      setConstants(m._present, 8);
      if (m.indexed) gl.drawElements(mode, count, m._itype, first * m._isize);
      else gl.drawArrays(mode, first, count);
      gl.bindVertexArray(null);
      GL.stats.draws++; GL.stats.tris += countTris(mode, count);
    };

    m.free = function () {
      if (m._gen === gen && gl) {
        for (let i = 0; i < 8; i++) if (m._bufs[i]) gl.deleteBuffer(m._bufs[i]);
        if (m._ibo) gl.deleteBuffer(m._ibo);
        gl.deleteVertexArray(m.vao);
      }
      m._bufs.fill(null); m._present.fill(false); m._ibo = null; m.vao = null; m.count = 0;
    };

    for (let i = 0; i < 8; i++) if (spec[ATTR_NAMES[i]]) m.update(ATTR_NAMES[i], spec[ATTR_NAMES[i]]);
    if (spec.index) m.setIndex(spec.index);
    return m;
  };

  // ------------------------------------------------------------------------------------------ instancing
  GL.instances = function (mesh, capacity) {
    if (!gl) throw new Error('GL.instances: call GL.init first');
    capacity = Math.max(1, capacity | 0);
    const inst = {
      mesh, data: new Float32Array(16 * capacity), count: 0, capacity,
      vao: gl.createVertexArray(), _buf: gl.createBuffer(), _gpuCap: 0, _ver: -1, _gen: gen,
    };
    const present = [false, false, false, false];

    function wire() {                                        // (re)build the VAO: the mesh's vertex buffers + the instance buffer
      gl.bindVertexArray(inst.vao);
      for (let i = 0; i < 4; i++) {
        present[i] = !!mesh._bufs[i];
        if (present[i]) {
          gl.bindBuffer(gl.ARRAY_BUFFER, mesh._bufs[i]);
          gl.enableVertexAttribArray(i);
          gl.vertexAttribPointer(i, ATTR_SIZE[i], gl.FLOAT, false, 0, 0);
        } else gl.disableVertexAttribArray(i);
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, inst._buf);
      for (let k = 0; k < 4; k++) {
        gl.enableVertexAttribArray(4 + k);
        gl.vertexAttribPointer(4 + k, 4, gl.FLOAT, false, 64, 16 * k);
        gl.vertexAttribDivisor(4 + k, 1);
      }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, mesh._ibo);     // null when the mesh is not indexed
      gl.bindVertexArray(null);
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
      inst._ver = mesh._ver;
    }

    inst.ensure = function (cap) {
      cap = cap | 0;
      if (cap <= inst.capacity) return inst;
      const grown = Math.max(cap, Math.ceil(inst.capacity * 1.5));
      const d = new Float32Array(16 * grown);
      d.set(inst.data);
      inst.data = d; inst.capacity = grown;
      return inst;
    };

    inst.upload = function (first, count) {
      first = first || 0;
      if (count === undefined) count = inst.count;
      gl.bindBuffer(gl.ARRAY_BUFFER, inst._buf);
      if (inst._gpuCap < inst.capacity) {                    // first upload, or the array grew: (re)allocate and send everything
        gl.bufferData(gl.ARRAY_BUFFER, inst.data, gl.DYNAMIC_DRAW);
        inst._gpuCap = inst.capacity;
      } else if (count > 0) {
        if (first + count > inst.capacity) count = inst.capacity - first;
        if (count > 0) gl.bufferSubData(gl.ARRAY_BUFFER, first * 64, inst.data, first * 16, count * 16);
      }
      gl.bindBuffer(gl.ARRAY_BUFFER, null);
      return inst;
    };

    inst.draw = function (count) {
      if (count === undefined) count = inst.count;
      if (!(count > 0) || !(mesh.count > 0)) return;
      if (inst._ver !== mesh._ver) wire();
      if (inst._gpuCap < inst.capacity) inst.upload(0, 0);
      gl.bindVertexArray(inst.vao);
      setConstants(present, 4);
      if (mesh.indexed) gl.drawElementsInstanced(gl.TRIANGLES, mesh.count, mesh._itype, 0, count);
      else gl.drawArraysInstanced(gl.TRIANGLES, 0, mesh.count, count);
      gl.bindVertexArray(null);
      GL.stats.draws++; GL.stats.instances += count; GL.stats.tris += ((mesh.count / 3) | 0) * count;
    };

    inst.free = function () {
      if (inst._gen === gen && gl) { gl.deleteBuffer(inst._buf); gl.deleteVertexArray(inst.vao); }
      inst._buf = null; inst.vao = null; inst.count = 0;
    };

    wire();
    return inst;
  };

  // ------------------------------------------------------------------------------------------ render targets
  function restoreFbo() {
    gl.bindFramebuffer(gl.FRAMEBUFFER, boundFbo);
  }
  function fboStatusName(s) {
    const names = {};
    names[gl.FRAMEBUFFER_INCOMPLETE_ATTACHMENT] = 'INCOMPLETE_ATTACHMENT';
    names[gl.FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT] = 'INCOMPLETE_MISSING_ATTACHMENT';
    names[gl.FRAMEBUFFER_INCOMPLETE_DIMENSIONS] = 'INCOMPLETE_DIMENSIONS';
    names[gl.FRAMEBUFFER_UNSUPPORTED] = 'UNSUPPORTED';
    names[gl.FRAMEBUFFER_INCOMPLETE_MULTISAMPLE] = 'INCOMPLETE_MULTISAMPLE';
    return names[s] || ('0x' + s.toString(16));
  }

  GL.target = function (w, h, opts) {
    if (!gl) throw new Error('GL.target: call GL.init first');
    opts = opts || {};
    let samples = Math.min(opts.samples | 0, GL.caps.maxSamples);
    if (samples < 2) samples = 0;
    if (opts.depthTex && (opts.samples | 0) > 0) throw new Error('GL.target: depthTex cannot be combined with samples > 0');
    const t = {
      fbo: gl.createFramebuffer(), tex: null, depthTex: null, w: 0, h: 0, samples,
      hdr: !!(opts.hdr && GL.caps.hdr), linear: opts.linear !== false,
      deep: !!(opts.hdr && opts.deep && !GL.caps.hdr),
      _rbC: null, _rbD: null, _gen: gen,
    };
    const wantDepth = !!(opts.depth || opts.depthTex || samples > 0);
    const fmt = t.hdr ? gl.RGBA16F : t.deep ? gl.RGB10_A2 : gl.RGBA8;

    function release() {
      if (t.tex) gl.deleteTexture(t.tex);
      if (t.depthTex) gl.deleteTexture(t.depthTex);
      if (t._rbC) gl.deleteRenderbuffer(t._rbC);
      if (t._rbD) gl.deleteRenderbuffer(t._rbD);
      t.tex = t.depthTex = t._rbC = t._rbD = null;
    }
    function alloc(nw, nh) {
      nw = Math.max(1, nw | 0); nh = Math.max(1, nh | 0);
      release();
      t.w = nw; t.h = nh;
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.activeTexture(gl.TEXTURE0);
      if (samples > 0) {
        t._rbC = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, t._rbC);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, fmt, nw, nh);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, t._rbC);
        t._rbD = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, t._rbD);
        gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, gl.DEPTH_COMPONENT24, nw, nh);
        gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, t._rbD);
      } else {
        t.tex = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, t.tex);
        gl.texStorage2D(gl.TEXTURE_2D, 1, fmt, nw, nh);
        const filter = t.linear ? gl.LINEAR : gl.NEAREST;
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t.tex, 0);
        if (opts.depthTex) {
          t.depthTex = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, t.depthTex);
          gl.texStorage2D(gl.TEXTURE_2D, 1, gl.DEPTH_COMPONENT24, nw, nh);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
          gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, t.depthTex, 0);
        } else if (wantDepth) {
          t._rbD = gl.createRenderbuffer();
          gl.bindRenderbuffer(gl.RENDERBUFFER, t._rbD);
          gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, nw, nh);
          gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, t._rbD);
        }
        gl.bindTexture(gl.TEXTURE_2D, null);
      }
      gl.bindRenderbuffer(gl.RENDERBUFFER, null);
      const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
      restoreFbo();
      if (status !== gl.FRAMEBUFFER_COMPLETE && !gl.isContextLost()) {
        throw new Error('GL.target: framebuffer ' + fboStatusName(status) + ' (' + nw + 'x' + nh + ', ' + (t.hdr ? 'RGBA16F' : t.deep ? 'RGB10_A2' : 'RGBA8') + ', samples ' + samples + ')');
      }
    }

    t.bind = function () {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.viewport(0, 0, t.w, t.h);
      boundFbo = t.fbo; boundW = t.w; boundH = t.h;
    };
    t.resize = function (nw, nh) {
      nw = Math.max(1, nw | 0); nh = Math.max(1, nh | 0);
      if (nw === t.w && nh === t.h) return t;
      alloc(nw, nh);
      if (boundFbo === t.fbo) { boundW = t.w; boundH = t.h; gl.viewport(0, 0, t.w, t.h); }
      return t;
    };
    /** Resolve / copy the colour buffer into `dst` (single-sample, same size and format, never the canvas). */
    t.blitTo = function (dst) {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, t.fbo);
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, dst.fbo);
      gl.blitFramebuffer(0, 0, t.w, t.h, 0, 0, dst.w, dst.h, gl.COLOR_BUFFER_BIT, gl.NEAREST);
      restoreFbo();
    };
    t.free = function () {
      if (t._gen === gen && gl) { release(); gl.deleteFramebuffer(t.fbo); }
      if (boundFbo === t.fbo) boundFbo = null;
      t.fbo = null; t.tex = t.depthTex = t._rbC = t._rbD = null;
    };

    alloc(w, h);
    return t;
  };

  GL.depthTarget = function (size) {
    if (!gl) throw new Error('GL.depthTarget: call GL.init first');
    const t = { fbo: gl.createFramebuffer(), tex: null, depthTex: null, w: 0, h: 0, samples: 0, hdr: false, _gen: gen };
    function alloc(s) {
      s = Math.max(1, s | 0);
      if (t.depthTex) gl.deleteTexture(t.depthTex);
      t.w = t.h = s;
      t.depthTex = gl.createTexture();
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, t.depthTex);
      gl.texStorage2D(gl.TEXTURE_2D, 1, gl.DEPTH_COMPONENT24, s, s);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_MODE, gl.COMPARE_REF_TO_TEXTURE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_COMPARE_FUNC, gl.LEQUAL);
      gl.bindTexture(gl.TEXTURE_2D, null);
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.TEXTURE_2D, t.depthTex, 0);
      gl.drawBuffers([gl.NONE]);
      gl.readBuffer(gl.NONE);
      const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
      restoreFbo();
      if (status !== gl.FRAMEBUFFER_COMPLETE && !gl.isContextLost()) throw new Error('GL.depthTarget: framebuffer ' + fboStatusName(status) + ' (' + s + ')');
    }
    t.bind = function () {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.fbo);
      gl.viewport(0, 0, t.w, t.h);
      boundFbo = t.fbo; boundW = t.w; boundH = t.h;
    };
    t.resize = function (s) {
      s = Math.max(1, s | 0);
      if (s !== t.w) { alloc(s); if (boundFbo === t.fbo) { boundW = boundH = s; gl.viewport(0, 0, s, s); } }
      return t;
    };
    t.free = function () {
      if (t._gen === gen && gl) { if (t.depthTex) gl.deleteTexture(t.depthTex); gl.deleteFramebuffer(t.fbo); }
      if (boundFbo === t.fbo) boundFbo = null;
      t.fbo = null; t.depthTex = null;
    };
    alloc(size);
    return t;
  };

  GL.bindTex = function (unit, texture) {
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, texture || null);
    if (unit !== 0) gl.activeTexture(gl.TEXTURE0);
  };

  GL.toScreen = function () {
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight);
    boundFbo = null; boundW = gl.drawingBufferWidth; boundH = gl.drawingBufferHeight;
  };

  GL.fsTri = function () {
    gl.bindVertexArray(fsVao);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
    gl.bindVertexArray(null);
    GL.stats.draws++; GL.stats.tris++;
  };

  // One triangle covering the screen, at the far plane (so a depth-tested fullscreen pass only touches empty pixels).
  GL.FS_VS = [
    'out vec2 vUv;',
    'void main() {',
    '  vUv = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));',
    '  gl_Position = vec4(vUv * 2.0 - 1.0, 1.0, 1.0);',
    '}',
  ].join('\n');

  // ------------------------------------------------------------------------------------------ chunk <noise>
  GL.chunk('noise', [
    '// ---- <noise> : integer-hashed value noise; identical on every GPU, no trigonometry',
    'float dm_hashU(uint n) {',
    '  n = (n ^ (n >> 15u)) * 0x85ebca6bu;',
    '  n = (n ^ (n >> 13u)) * 0xc2b2ae35u;',
    '  n ^= n >> 16u;',
    '  return float(n >> 8u) * (1.0 / 16777216.0);',
    '}',
    'uint dm_mix3(ivec3 q) { return (uint(q.x) * 0x27d4eb2du) ^ (uint(q.y) * 0x165667b1u) ^ (uint(q.z) * 0x9e3779b1u); }',
    'float hash13(vec3 p) { return dm_hashU(dm_mix3(floatBitsToInt(p + 0.0))); }',
    'vec3 hash33(vec3 p) {',
    '  uint n = dm_mix3(floatBitsToInt(p + 0.0));',
    '  return vec3(dm_hashU(n), dm_hashU(n ^ 0x68bc21ebu), dm_hashU(n ^ 0x02e5be93u));',
    '}',
    'float vnoise(vec3 p) {',
    '  vec3 pf = floor(p);',
    '  vec3 f = p - pf;',
    '  ivec3 q = ivec3(pf);',
    '  vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);',
    '  uint x0 = uint(q.x) * 0x27d4eb2du, x1 = x0 + 0x27d4eb2du;',
    '  uint y0 = uint(q.y) * 0x165667b1u, y1 = y0 + 0x165667b1u;',
    '  uint z0 = uint(q.z) * 0x9e3779b1u, z1 = z0 + 0x9e3779b1u;',
    '  return mix(mix(mix(dm_hashU(x0 ^ y0 ^ z0), dm_hashU(x1 ^ y0 ^ z0), u.x), mix(dm_hashU(x0 ^ y1 ^ z0), dm_hashU(x1 ^ y1 ^ z0), u.x), u.y),',
    '             mix(mix(dm_hashU(x0 ^ y0 ^ z1), dm_hashU(x1 ^ y0 ^ z1), u.x), mix(dm_hashU(x0 ^ y1 ^ z1), dm_hashU(x1 ^ y1 ^ z1), u.x), u.y), u.z);',
    '}',
    'const mat3 DM_OCT = mat3(0.00, 1.60, 1.20, -1.60, 0.72, -0.96, -1.20, -0.96, 1.28);   // rotate and double per octave',
    'float fbm3(vec3 p) {',
    '  float s = 0.5 * vnoise(p); p = DM_OCT * p + 11.7;',
    '  s += 0.25 * vnoise(p); p = DM_OCT * p + 5.3;',
    '  s += 0.125 * vnoise(p);',
    '  return s * 1.142857;',
    '}',
    'float fbm5(vec3 p) {',
    '  float s = 0.5 * vnoise(p); p = DM_OCT * p + 11.7;',
    '  s += 0.25 * vnoise(p); p = DM_OCT * p + 5.3;',
    '  s += 0.125 * vnoise(p); p = DM_OCT * p + 2.9;',
    '  s += 0.0625 * vnoise(p); p = DM_OCT * p + 8.1;',
    '  s += 0.03125 * vnoise(p);',
    '  return s * 1.032258;',
    '}',
  ].join('\n'));

  // ------------------------------------------------------------------------------------------ chunk <clouds>
  GL.chunk('clouds', [
    '#include <noise>',
    '// ---- <clouds> : one cloud field for the cloud shell (5 octaves) and for cloud shadows (3 octaves of the same field)',
    'float dm_cloud(vec3 d, bool fine) {',
    '  float amount = uFx.y;',
    '  if (amount <= 0.002) return 0.0;',
    '  float storm = 0.0;',
    '  vec3 q = d;',
    '  for (int i = 0; i < 8; i++) {',
    '    vec4 s = uStorms[i];',
    '    if (s.w > 0.0) {',
    '      float k = 1.0 - acos(clamp(dot(d, s.xyz), -1.0, 1.0)) / s.w;      // 1 at the eye of the storm, 0 at its rim',
    '      if (k > 0.0) {',
    '        float a = k * k * 2.6, ca = cos(a), sa = sin(a);               // swirl the field around the centre',
    '        q = q * ca + cross(s.xyz, q) * sa + s.xyz * (dot(s.xyz, q) * (1.0 - ca));',
    '        storm = max(storm, k);',
    '      }',
    '    }',
    '  }',
    '  float t = uTime.z;',
    '  float a = t * 0.5, ca = cos(a), sa = sin(a);                          // prevailing wind: slow drift round the axis',
    '  q = vec3(q.x * ca - q.z * sa, q.y, q.x * sa + q.z * ca);',
    '  vec3 lo = q * vec3(1.5, 2.1, 1.5) + vec3(0.0, t * 0.21, 0.0);          // weather scale: where the systems are, how they curl',
    '  float w1 = vnoise(lo + 3.7), w2 = vnoise(lo + vec3(31.4, 17.1, -9.3));',
    '  vec3 p = q * vec3(3.3, 4.4, 3.3) + vec3(0.0, t * 0.35, 0.0);          // cloud scale: clumps, wider than they are tall',
    '  p += vec3(-q.z, 0.0, q.x) * ((w1 - 0.5) * 1.5) + vec3(0.0, (w2 - 0.5) * 1.0, 0.0);',
    '  float n = 0.5 * vnoise(p); p = DM_OCT * p + 11.7;',
    '  n += 0.29 * vnoise(p); p = DM_OCT * p + 5.3;',
    '  n += 0.17 * vnoise(p);',
    '  if (fine) {',
    '    p = DM_OCT * p + 2.9; n += 0.10 * vnoise(p);',
    '    p = DM_OCT * p + 8.1; n += 0.06 * vnoise(p);',
    '  } else n += 0.08;',
    '  n *= 0.892857;',
    '  float lat = asin(clamp(d.y, -1.0, 1.0)) + (w1 - 0.5) * 0.45;',
    '  float band = cos(6.0 * lat);                                          // wet equator and mid-latitudes, clear subtropics and poles',
    '  float cov = clamp(amount * (0.82 + 0.40 * band + 0.9 * (w2 - 0.5)) + storm * 0.6, 0.0, 1.0);',
    '  float th = mix(0.80, 0.36, cov);',
    '  float c = smoothstep(th, th + 0.12, n);',
    '  if (fine) {                                                           // thin high streaks along the wind, shell only',
    '    float ci = vnoise(vec3(lo.x * 2.4, lo.y * 6.5, lo.z * 2.4) + p * 0.11 + 7.7);',
    '    c = max(c, 0.30 * smoothstep(0.42, 0.80, ci) * smoothstep(th - 0.17, th + 0.02, n));',
    '  }',
    '  c *= 1.0 - smoothstep(0.90, 0.97, storm);                             // the eye',
    '  return c * smoothstep(0.0, 0.08, amount);',
    '}',
    'float cloudCover(vec3 dir) { return dm_cloud(dir, uScreen.w > 0.5); }',
  ].join('\n'));

  // ------------------------------------------------------------------------------------------ chunk <lighting>
  GL.chunk('lighting', [
    '#include <clouds>',
    '// ---- <lighting> : the shared look. Linear HDR; a white surface facing the noon sun returns 2.5.',
    'uniform sampler2DShadow uShadowMap;',
    'const float DM_ATMO_H = 4.0;                    // scale height of the air, world units',
    'const vec3 DM_MOON = vec3(0.25, 0.34, 0.56);     // moonlight on a surface that faces the moon',
    'const vec3 DM_NIGHT = vec3(0.11, 0.165, 0.32);   // night-sky fill from straight above',
    '',
    'vec3 moonDir() {',
    '  vec3 s = uSunDir.xyz;',
    '  vec3 side = cross(s, vec3(0.0, 1.0, 0.0));',
    '  float l = length(side);',
    '  side = l > 0.02 ? side / l : vec3(1.0, 0.0, 0.0);',
    '  return normalize(side * 0.42 + vec3(0.0, 0.38, 0.0) - s * 0.82);',
    '}',
    'float dayAt(vec3 worldPos) { return smoothstep(-0.15, 0.25, dot(normalize(worldPos), uSunDir.xyz)); }',
    '',
    '// how much air this world has: thin while it has no sea (uFx.z = ocean fraction)',
    'float dm_atmoK() { return mix(0.22, 1.0, smoothstep(0.0, 0.4, uFx.z)); }',
    '// colour of the sunlight that reaches a point where the sun stands at cosine mu above the horizon: white at noon,',
    '// gold, then deep orange at the terminator (k = extinction per unit air mass); dust dims and browns it',
    'vec3 dm_sunTintK(float mu, vec3 k) {',
    '  float air = 1.0 / (max(mu, -0.09) + 0.15) - 0.87;',
    '  return exp(-k * air);',
    '}',
    'vec3 dm_sunK() { return vec3(0.05, 0.13, 0.30) * mix(0.4, 1.0, dm_atmoK()); }',
    'vec3 dm_dustTint() { return mix(vec3(1.0), vec3(0.60, 0.46, 0.34), uWorld.y); }',
    'float dm_sunVis(float mu) { return smoothstep(-0.07, 0.09, mu); }      // the planet itself hides the sun: analytic, soft',
    '',
    'float sunShadow(vec3 worldPos, vec3 normal) {',
    '  vec3 r0 = vec3(uShadowMat[0][0], uShadowMat[1][0], uShadowMat[2][0]);',
    '  float size = float(textureSize(uShadowMap, 0).x);',
    '  float texel = 1.0 / (max(length(r0), 1e-6) * size);                 // world units per shadow texel',
    '  vec3 sc = (uShadowMat * vec4(worldPos + normal * (1.5 * texel), 1.0)).xyz;',
    '  vec2 e = abs(sc.xy - 0.5) * 2.0;',
    '  float edge = max(e.x, e.y);',
    '  if (edge >= 1.0 || sc.z <= 0.0 || sc.z >= 1.0) return 1.0;',
    '  float it = 1.0 / size;',
    '  float s;',
    '  if (uScreen.w < 0.5) {',
    '    s = textureLod(uShadowMap, sc, 0.0);',
    '  } else if (uScreen.w < 1.5) {',
    '    float o = 0.5 * it;',
    '    s = 0.25 * (textureLod(uShadowMap, vec3(sc.xy + vec2(-o, -o), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(o, -o), sc.z), 0.0)',
    '              + textureLod(uShadowMap, vec3(sc.xy + vec2(-o, o), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(o, o), sc.z), 0.0));',
    '  } else {',
    '    s = 0.25 * textureLod(uShadowMap, sc, 0.0);',
    '    s += 0.125 * (textureLod(uShadowMap, vec3(sc.xy + vec2(-it, 0.0), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(it, 0.0), sc.z), 0.0)',
    '                + textureLod(uShadowMap, vec3(sc.xy + vec2(0.0, -it), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(0.0, it), sc.z), 0.0));',
    '    s += 0.0625 * (textureLod(uShadowMap, vec3(sc.xy + vec2(-it, -it), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(it, -it), sc.z), 0.0)',
    '                 + textureLod(uShadowMap, vec3(sc.xy + vec2(-it, it), sc.z), 0.0) + textureLod(uShadowMap, vec3(sc.xy + vec2(it, it), sc.z), 0.0));',
    '  }',
    '  return mix(s, 1.0, smoothstep(0.92, 1.0, edge));',
    '}',
    '',
    'float cloudShade(vec3 worldPos) {',
    '  float r = length(worldPos), rc = uPlanet.z;',
    '  float under = smoothstep(rc - 0.5, rc - 4.0, r);                    // nothing above the cloud deck is shaded',
    '  if (under <= 0.0 || uFx.y <= 0.002) return 1.0;',
    '  vec3 L = uSunDir.xyz;',
    '  float b = dot(worldPos, L);',
    '  float t = -b + sqrt(max(b * b - r * r + rc * rc, 0.0));             // where the ray toward the sun leaves the cloud deck',
    '  float c = dm_cloud(normalize(worldPos + L * t), false);',
    '  return 1.0 - 0.55 * under * mix(0.6, 1.0, uFx.x) * c;',
    '}',
    '',
    'vec3 dm_flashAt(vec3 worldPos, out vec3 toLight) {',
    '  toLight = vec3(0.0, 1.0, 0.0);',
    '  if (uFlash.w <= 0.0) return vec3(0.0);',
    '  vec3 d = uFlash.xyz - worldPos;',
    '  float d2 = dot(d, d);',
    '  float a = max(1.0 - d2 / (uFlash.w * uFlash.w), 0.0);',
    '  toLight = d * inversesqrt(max(d2, 1e-4));',
    '  return uFlashCol.rgb * (a * a);',
    '}',
    '// everything that is not direct sun: blue sky fill by day, a warm band at the terminator, moon and sky glow at night',
    'vec3 dm_ambient(vec3 n, vec3 up, float mu) {',
    '  float day = smoothstep(-0.15, 0.25, mu);',
    '  float hemi = 0.5 + 0.5 * dot(n, up);',
    '  float k = dm_atmoK();',
    '  vec3 zen = mix(vec3(0.15), vec3(0.19, 0.30, 0.52), k);',
    '  zen *= mix(vec3(1.0), vec3(1.12, 0.70, 0.46), (1.0 - smoothstep(0.0, 0.5, mu)) * k);   // toward sunset the sky itself turns from blue to peach',
    '  vec3 sky = mix(vec3(0.085, 0.075, 0.065), zen, hemi);',
    '  sky = mix(sky, vec3(0.17, 0.15, 0.13), uWorld.y * 0.7);',
    '  float tw = smoothstep(-0.24, -0.02, mu) * (1.0 - smoothstep(0.0, 0.40, mu));',
    '  vec3 warm = vec3(0.58, 0.25, 0.085) * (tw * (0.4 + 0.6 * hemi) * (0.5 + 0.5 * k));',
    '  float nm = clamp((dot(n, moonDir()) + 0.3) / 1.3, 0.0, 1.0);',
    '  vec3 night = DM_NIGHT * (0.5 + 0.5 * hemi) + DM_MOON * nm;',
    '  vec3 glow = vec3(0.50, 0.14, 0.04) * (uWorld.x * (1.0 - 0.6 * hemi)); // a molten world lights itself from below',
    '  return (sky * day + warm + night * (1.0 - smoothstep(-0.18, 0.18, mu))) * uSunCol.w + glow;',
    '}',
    '',
    'vec3 lightAt(vec3 worldPos) {',
    '  vec3 up = normalize(worldPos);',
    '  float mu = dot(up, uSunDir.xyz);',
    '  float day = smoothstep(-0.15, 0.25, mu);',
    '  vec3 light = dm_ambient(up, up, mu);',
    '  if (day > 0.0) light += uSunCol.rgb * dm_sunTintK(mu, dm_sunK()) * dm_dustTint() * (day * cloudShade(worldPos));',
    '  vec3 toLight;',
    '  light += dm_flashAt(worldPos, toLight);',
    '  return light * uSunDir.w;',
    '}',
    '',
    'vec3 shade(vec3 albedo, vec3 normal, vec3 worldPos, float specular, float emissive) {',
    '  vec3 up = normalize(worldPos);',
    '  vec3 L = uSunDir.xyz;',
    '  float mu = dot(up, L);',
    '  float ndl = dot(normal, L);',
    '  float vis = dm_sunVis(mu);',
    '  vec3 sunC = uSunCol.rgb * dm_sunTintK(mu, dm_sunK()) * dm_dustTint();',
    '  float lit = 0.0;',
    '  if (vis > 0.0 && (ndl > 0.0 || specular > 0.0)) lit = vis * sunShadow(worldPos, normal) * cloudShade(worldPos);',
    '  vec3 light = sunC * (max(ndl, 0.0) * lit) + dm_ambient(normal, up, mu);',
    '  vec3 toLight;',
    '  vec3 fl = dm_flashAt(worldPos, toLight);',
    '  light += fl * (0.35 + 0.65 * max(dot(normal, toLight), 0.0));',
    '  vec3 c = albedo * light;',
    '  if (specular > 0.0) {',
    '    vec3 V = normalize(uCamPos.xyz - worldPos);',
    '    float sp = mix(12.0, 160.0, specular);',
    '    float k = specular * (sp + 2.0) * 0.044;',
    '    float day = smoothstep(-0.15, 0.25, mu);',
    '    c += sunC * (pow(max(dot(normal, normalize(L + V)), 0.0), sp) * k * lit * step(0.0, ndl));',
    '    c += DM_MOON * (pow(max(dot(normal, normalize(moonDir() + V)), 0.0), sp) * k * 0.3 * (1.0 - day) * uSunCol.w);',
    '  }',
    '  c += albedo * emissive;',
    '  return c * uSunDir.w;',
    '}',
    '',
    '// Single scattering along a view ray through an exponential atmosphere, with extinction: thin air is blue, thick air',
    '// saturates toward white, air lit by a low sun is orange. The ray is split at its closest approach to the planet; the',
    '// far half fades out for rays that skim the ground, so the halo (tMax = far side of the shell) meets the haze on the',
    '// surface (tMax = the surface) without a step at the limb. `tau` returns the optical depth for the transmittance.',
    'vec3 dm_air(vec3 ro, vec3 rd, float tMax, out vec3 tau) {',
    '  tau = vec3(0.0);',
    '  float rp = uPlanet.x, ra = uPlanet.w;',
    '  float b = dot(ro, rd), rr = dot(ro, ro);',
    '  float disc = b * b - rr + ra * ra;',
    '  if (disc <= 0.0) return vec3(0.0);',
    '  float sq = sqrt(disc);',
    '  float t0 = max(-b - sq, 0.0), t1 = min(-b + sq, tMax);',
    '  if (t1 <= t0) return vec3(0.0);',
    '  float tp = clamp(-b, t0, t1);',
    '  float lenF = tp - t0, lenB = t1 - tp;',
    '  float hp = sqrt(max(rr - b * b, 0.0)) - rp;',
    '  float wBack = mix(1.0, smoothstep(-1.0, 3.0, hp), smoothstep(0.0, 12.0, lenF));',
    '  vec3 L = uSunDir.xyz;',
    '  float c = dot(rd, L);',
    '  float phR = 0.75 * (1.0 + c * c);',
    '  float cm = max(c, 0.0), c2 = cm * cm, c6 = c2 * c2 * c2, c12 = c6 * c6;',
    '  float phM = 0.5 * c6 + 2.5 * c12 * c12 * c12;',
    '  float dust = uWorld.y;',
    '  float kd = dm_atmoK() * mix(0.6, 1.0, smoothstep(6.0, 60.0, uCamPos.w));',
    '  vec3 bR = vec3(0.0051, 0.0114, 0.0254) * kd;',
    '  float bM = 0.0032 * kd * (1.0 + 8.0 * dust);',
    '  vec3 sunK = dm_sunK();',
    '  vec3 sunL = uSunCol.rgb * 0.55 * mix(vec3(1.0), vec3(0.75, 0.62, 0.50), dust) * mix(vec3(1.0), vec3(1.25, 0.55, 0.30), uWorld.x);',
    '  vec3 srcR = bR * phR, srcM = mix(vec3(1.0), vec3(0.85, 0.70, 0.55), dust) * (bM * phM);',
    '  vec3 glow = (bR + bM) * vec3(0.020, 0.034, 0.060) * uSunCol.w;        // night airglow: the limb stays readable',
    '  float top = exp(-(ra - rp) / DM_ATMO_H);',
    '  int n1 = uScreen.w < 0.5 ? 3 : 4, n2 = uScreen.w < 0.5 ? 2 : 3;',
    '  vec3 sum = vec3(0.0);',
    '  for (int i = 0; i < 7; i++) {',
    '    if (i >= n1 + n2) break;',
    '    float t, dt, w = 1.0;',
    '    if (i < n1) {                                                       // camera -> closest approach, denser toward it',
    '      float j = float(n1 - 1 - i), inv = 1.0 / float(n1);',
    '      float u0 = j * inv, u1 = (j + 1.0) * inv, um = (j + 0.5) * inv;',
    '      t = tp - lenF * um * um; dt = lenF * (u1 * u1 - u0 * u0);',
    '    } else {                                                            // closest approach -> out the far side',
    '      float j = float(i - n1), inv = 1.0 / float(n2);',
    '      float u0 = j * inv, u1 = (j + 1.0) * inv, um = (j + 0.5) * inv;',
    '      t = tp + lenB * um * um; dt = lenB * (u1 * u1 - u0 * u0); w = wBack;',
    '    }',
    '    if (dt * w <= 0.0) continue;',
    '    vec3 p = ro + rd * t;',
    '    float r = length(p), h = r - rp;',
    '    float rho = max(exp(-max(h, -2.0) / DM_ATMO_H) - top, 0.0) * w;',
    '    float mu = dot(p, L) / r + 0.02 * max(h, 0.0);                      // high air sees the sun a little longer',
    '    vec3 sun = dm_sunTintK(mu, sunK * vec3(1.0, 1.9, 1.9)) * (dm_sunVis(mu) * (1.0 + 0.7 * (1.0 - smoothstep(0.0, 0.3, mu))));   // scattered light reddens faster than direct light; dusk glows',
    '    vec3 dtau = (bR + bM * 1.1) * (rho * dt);',
    '    sum += exp(-(tau + 0.5 * dtau)) * ((srcR + srcM) * sun * sunL + glow) * (rho * dt);',
    '    tau += dtau;',
    '  }',
    '  return sum * uSunDir.w;',
    '}',
    'vec3 inscatter(vec3 rayOrigin, vec3 rayDir, float tMax) {',
    '  vec3 tau;',
    '  return dm_air(rayOrigin, rayDir, tMax, tau);',
    '}',
    'vec3 atmosphere(vec3 color, vec3 worldPos) {',
    '  vec3 v = worldPos - uCamPos.xyz;',
    '  float d = max(length(v), 1e-4);',
    '  vec3 tau;',
    '  vec3 ins = dm_air(uCamPos.xyz, v / d, d, tau);',
    '  return color * exp(-tau) + ins;',
    '}',
  ].join('\n'));
})(globalThis.DM = globalThis.DM || {});
