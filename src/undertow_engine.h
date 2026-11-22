// bsbr undertow: kick + rumble instrument. Native port of the web engine (undertow-worklet.js), 1:1.
// Chain: synth kick -> finish (EQ, comp, limiter) -> notch pre -> drive (bypass / valve / sine / analog, 4x)
//        -> notch post -> + beater click -> dry / haze (FDN reverb + drift) / echo (synced ping-pong)
//        -> each fx path: HPF -> LPF -> OTT -> duck -> sum -> soft clip or limiter -> bsbr seal -> vol -> ceiling.
// No allocations in process(). Latency: 10 ms (two 5 ms look-ahead stages).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

namespace undertow {

static constexpr double PI = 3.14159265358979323846, TAU = 2 * PI;
static const double TANH15 = std::tanh(1.5);
static constexpr double Q4[2] = {0.5412, 1.3066};
static constexpr double OTT_W[3] = {0.8, 1, 0.5};
static constexpr double DIVS[5] = {1.0 / 16, 1.0 / 8, 3.0 / 16, 1.0 / 4, 3.0 / 8};

inline double clampd(double x, double a, double b) { return x < a ? a : x > b ? b : x; }
inline double tanhf_(double x) { if (x > 9) return 1; if (x < -9) return -1; double e = std::exp(2 * x); return (e - 1) / (e + 1); }
inline double dbToG(double d) { return std::pow(10.0, d / 20); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double sgn(double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }

struct SVF {
  double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1.414, lp = 0, hp = 0;
  void set(double fc, double q, double sr) {
    double g = std::tan(PI * clampd(fc, 10, sr * 0.45) / sr);
    k = 1 / q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  double run(double x) {
    double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; hp = x - k * v1 - v2; return v2;
  }
  void clear() { ic1 = ic2 = lp = hp = 0; }
};

struct Shelf {
  double ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0, t0 = 1, t1 = 0, t2 = 0;
  bool low = true, inited = false; double kf = -1, kg = -1e9; int ktype = -1;
  void set(bool isLow, double f, double gdb, double sr) {
    double g2 = std::round(gdb * 100) / 100;
    if (ktype == (int)isLow && kf == f && kg == g2) return;
    ktype = (int)isLow; kf = f; kg = g2;
    double A = std::pow(10.0, gdb / 40), k = std::sqrt(2.0), t = std::tan(PI * std::min(f, sr * 0.45) / sr);
    double g = isLow ? t / std::sqrt(A) : t * std::sqrt(A);
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2;
    if (isLow) { t0 = 1; t1 = k * (A - 1); t2 = A * A - 1; } else { t0 = A * A; t1 = k * (1 - A) * A; t2 = 1 - A * A; }
    if (!inited) { m0 = t0; m1 = t1; m2 = t2; inited = true; }
  }
  double run(double x) {
    double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
    m0 += (t0 - m0) * 0.005; m1 += (t1 - m1) * 0.005; m2 += (t2 - m2) * 0.005;
    return m0 * x + m1 * v1 + m2 * v2;
  }
  void clear() { ic1 = ic2 = 0; }
};

struct Smooth {
  double v = 0, k = 0;
  void init(double v0, double ms, double sr) { v = v0; k = 1 - std::exp(-1 / (ms * 0.001 * sr)); }
  double tick(double t) { v += (t - v) * k; return v; }
};

struct Allpass {
  std::vector<float> buf; int i = 0; double g = 0.7;
  void init(int n, double gg) { buf.assign(std::max(1, n), 0.f); i = 0; g = gg; }
  double run(double x) { double d = buf[i], v = x + g * d; buf[i] = (float)v; i = (i + 1) % (int)buf.size(); return d - g * v; }
  void clear() { std::fill(buf.begin(), buf.end(), 0.f); }
};

struct LookLim {
  int n = 1, i = 0, cnt = 0; std::vector<float> bL, bR;
  double ceil = 1, held = 0, g = 1, att = 0, rel = 0, hr = 0, red = 0;
  void init(double sr, double c, double relMs) {
    n = std::max(1, (int)std::round(0.005 * sr)); bL.assign(n, 0.f); bR.assign(n, 0.f); i = 0;
    ceil = c; held = 0; cnt = 0; g = 1;
    att = 1 - std::exp(-1 / (n / 3.0)); rel = 1 - std::exp(-1 / (relMs * 0.001 * sr)); hr = std::exp(-1 / (relMs * 0.001 * sr)); red = 0;
  }
  void run(double l, double r, double* out) {
    double a = std::max(std::abs(l), std::abs(r));
    if (a >= held) { held = a; cnt = n; } else if (cnt > 0) cnt--; else held = a + (held - a) * hr;
    double gT = held > ceil ? ceil / held : 1;
    g += (gT - g) * (gT < g ? att : rel);
    double dl = bL[i], dr = bR[i]; bL[i] = (float)l; bR[i] = (float)r; i = (i + 1) % n;
    if (1 - g > red) red = 1 - g;
    out[0] = dl * g; out[1] = dr * g;
  }
  void clear() { std::fill(bL.begin(), bL.end(), 0.f); std::fill(bR.begin(), bR.end(), 0.f); held = 0; cnt = 0; g = 1; red = 0; }
};

struct Peak { // RBJ peak (type 0) or high-pass (type 1), coefficients cached
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; int kt = -1; double kf = -1, kq = -1, kg = -1e9;
  void set(int type, double f, double q, double gdb, double sr) {
    if (type == kt && f == kf && q == kq && gdb == kg) return;
    kt = type; kf = f; kq = q; kg = gdb;
    double A = std::pow(10.0, gdb / 40), w = TAU * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q);
    double B0, B1, B2, A0, A1, A2;
    if (type == 1) { B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; }
    else { B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; }
    b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
  }
  double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void clear() { z1 = z2 = 0; }
};

struct LP {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void init(double fc, double q, double sr) {
    double w = TAU * fc / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
    b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0; z1 = z2 = 0;
  }
  double run(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
  void clear() { z1 = z2 = 0; }
};

struct OS4 {
  LP u[3], d[3];
  void init(double sr) { const double qs[3] = {0.5176, 0.7071, 1.9319}; for (int j = 0; j < 3; j++) { u[j].init(17000, qs[j], sr * 4); d[j].init(17000, qs[j], sr * 4); } }
  template <class F> double run(double x, F&& fn) {
    double y = 0;
    for (int i = 0; i < 4; i++) {
      double v = i == 0 ? x * 4 : 0;
      for (int j = 0; j < 3; j++) v = u[j].run(v);
      v = fn(v);
      for (int j = 0; j < 3; j++) v = d[j].run(v);
      y = v;
    }
    return y;
  }
  void clear() { for (int j = 0; j < 3; j++) { u[j].clear(); d[j].clear(); } }
};

struct OTT {
  SVF lo[2][2], hi[2][2]; double env[3] = {0, 0, 0}, g[3] = {0, 0, 0}, down = 0, up = 0, bl[3] = {0, 0, 0}, br[3] = {0, 0, 0};
  void init(double sr) { for (int c = 0; c < 2; c++) for (int j = 0; j < 2; j++) { lo[c][j].set(88, 0.7071, sr); hi[c][j].set(2500, 0.7071, sr); } }
  void split(int c, double x, double* o) {
    double l = lo[c][1].run(lo[c][0].run(x)), hl = hi[c][1].run(hi[c][0].run(x)), h = x - hl;
    o[0] = l; o[1] = x - l - h; o[2] = h;
  }
  void run(double l, double r, double depth, double aC, double rC, double gC, double* out) {
    split(0, l, bl); split(1, r, br);
    double ol = 0, orr = 0;
    for (int b = 0; b < 3; b++) {
      double a = std::max(std::abs(bl[b]), std::abs(br[b]));
      double ac = b == 0 ? aC / 3 : aC, rc = b == 0 ? rC / 3 : rC;
      env[b] += (a - env[b]) * (a > env[b] ? ac : rc);
      double lv = 20 * std::log10(env[b] + 1e-9), gdb = 0;
      if (lv > -18) gdb = -std::min(12.0, (lv + 18) * (1 - 1.0 / 3));
      else if (lv > -70) gdb = std::min(10.0, (-36 - lv) * 0.5) * (lv > -36 ? 0 : clampd((lv + 70) / 10, 0, 1));
      gdb *= OTT_W[b] * depth;
      g[b] += (gdb - g[b]) * gC;
      double gg = dbToG(g[b]);
      ol += bl[b] * gg; orr += br[b] * gg;
      if (-g[b] > down) down = -g[b];
      if (g[b] > up) up = g[b];
    }
    out[0] = ol; out[1] = orr;
  }
  void clear() { for (int c = 0; c < 2; c++) for (int j = 0; j < 2; j++) { lo[c][j].clear(); hi[c][j].clear(); } for (int b = 0; b < 3; b++) env[b] = g[b] = 0; }
};

inline double triode(double u) { return (tanhf_(u * 0.8 + 0.6) - 0.5370) * 1.85; }
inline double safe(double x) { double a = std::abs(x); return a < 0.8 ? x : sgn(x) * (0.8 + 0.2 * tanhf_((a - 0.8) / 0.2)); }
inline double soft(double x) { double a = std::abs(x); return x / std::pow(1 + std::pow(a, 2.5), 0.4); }

// all parameters in engine units (same names and units as the web engine)
struct Params {
  double kpitch = 31, kpunch = 3, ksweep = 0.1, kcurve = 0.55, kshape = 0.3, klen = 1, katt = 0, kbody = 0, kdrop = 0;
  double click = 0.3, ctone = 3500, nfreq = 300, ndepth = 0; int npos = 1;
  int dtype = 2; double gain = 0.6, low = 2, high = -3;
  double vsend = 0.4, vsize = 0.7, vpre = 0.03, vmod = 0.5; int mvmode = 0; double mvamt = 0.4, mvrate = 0.15;
  double vhpf = 800, vdark = 10000, vduck = 0.8;
  int ddiv = 2; double dsend = 0.35, dfb = 0.45, dinto = 0.2, ehpf = 35, edark = 300, educk = 0.8;
  double ott = 0.4, otime = 0.5, drel = 0.25, dry = 1, wet = 0.8, clip = 0.1; int cmode = 0, listen = 0;
  int sealOn = 1; double squash = 0.3, crush = 0.12, smix = 0.5, vol = 0, bpm = 130;
};

struct Voice {
  bool on = false, fading = false; double age = 0, vel = 1, fade = 1, ph = 0, l1 = 0, l2 = 0, n1 = 0, n2 = 0;
  double kL = 1, kf0 = 50, ksw = 0.1, kcv = 0.5, kpu = 2, kdr = 0, ksh = 0, kat = 0.0003, kbo = 0, kc = 0.05;
};

struct FdnLine { std::vector<float> buf; int w = 0; double lp = 0, hp = 0; };
struct Slot { OS4 os; Shelf lo, hi; };

class Engine {
public:
  Params p;
  // meters for the UI (read from the message thread)
  float peakOut = 0; int hits = 0; float lastVel = 0;

  void prepare(double sampleRate) {
    sr = sampleRate;
    S4 = sr * 4;
    pre.assign((size_t)std::ceil(sr * 0.3) + 4, 0.f); pw = 0; preS.init(0.02 * sr, 80, sr);
    const int apn[4] = {142, 107, 379, 277};
    for (int j = 0; j < 4; j++) ap[j].init((int)std::round(apn[j] * sr / 44100), 0.7);
    const double fms[8] = {29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.7, 73.1};
    for (int j = 0; j < 8; j++) {
      fdnBase[j] = fms[j] * 0.001 * sr;
      fdn[j].buf.assign((size_t)(std::ceil(fdnBase[j] * 2.8) + std::ceil(0.004 * sr) + 32), 0.f); fdn[j].w = 0; fdn[j].lp = fdn[j].hp = 0;
      rLen[j].init(fdnBase[j], 400, sr); lph[j] = 0;
    }
    bodyC = 1 - std::exp(-TAU * 160 / sr);
    dlen = (int)std::ceil(sr * 3) + 8; dL.assign(dlen, 0.f); dR.assign(dlen, 0.f); dw = 0; dtime.init(sr * 0.2, 120, sr);
    sLPc = 1 - std::exp(-TAU * 7000 / sr);
    osL.init(sr); osR.init(sr);
    for (auto& s : slots) { s.os.init(sr); s.lo = Shelf(); s.hi = Shelf(); }
    lowSh4 = Shelf(); highSh4 = Shelf();
    ottV.init(sr); ottE.init(sr);
    ceilLim.init(sr, 0.944, 120); clipLim.init(sr, 0.9, 80);
    clipDL.assign(clipLim.n, 0.f); clipDR.assign(clipLim.n, 0.f); clipI = 0;
    clearScalars();
  }

  int latencySamples() const { return clipLim.n + ceilLim.n; }

  void trigger(double vel) {
    Voice& v0 = voices[0]; Voice& v1 = voices[1];
    if (v0.on) { v1 = v0; v1.fading = true; v1.fade = 1; }
    v0.kL = std::max(0.05, p.klen); v0.kf0 = 440 * std::pow(2.0, (std::round(p.kpitch) - 69) / 12); v0.ksw = std::max(0.005, p.ksweep);
    v0.kcv = 1 + p.kcurve * 7; v0.kpu = p.kpunch - 1; v0.kdr = p.kdrop / 12; v0.ksh = p.kshape; v0.kat = std::max(0.0003, p.katt); v0.kbo = p.kbody;
    v0.kc = 1 - std::exp(-TAU * (350 + 2600 * p.kshape) / sr);
    v0.on = true; v0.fading = false; v0.fade = 1; v0.age = 0; v0.vel = vel; v0.ph = 0; v0.l1 = v0.l2 = v0.n1 = v0.n2 = 0;
    ckAge = 0; ckVel = vel * p.click; ckPh = 0; ckL1 = ckL2 = 0;
    hits++; lastVel = (float)vel;
  }

  void allOff() { for (auto& v : voices) if (v.on && !v.fading) { v.fading = true; v.fade = 1; } }

  // renders n samples into L/R (adds nothing, overwrites)
  void process(float* L, float* R, int N) {
    blockSetup();
    for (int k = 0; k < N; k++) { double o[2]; tick(o); L[k] = (float)o[0]; R[k] = (float)o[1]; }
  }

private:
  double sr = 48000, S4 = 192000;
  Voice voices[2];
  double hpIn = 0, mLP = 0, redShow = 0, osRed = 0;
  std::vector<float> pre; int pw = 0; Smooth preS; Allpass ap[4];
  double fdnBase[8] = {}; FdnLine fdn[8]; Smooth rLen[8]; double lph[8] = {}, modL[8] = {};
  double vAlive = 0, bodyC = 0;
  double ckAge = 1e9, ckVel = 0, ckPh = 0, ckN = 0, ckL1 = 0, ckL2 = 0; SVF ckBP;
  SVF notch, notchPre; double nfS = 300, nPreG = 0, nPostG = 0;
  int slot = 0, curType = -1, oldType = 0, curMv = -1, oldMv = 0, curCm = -1, oldCm = 0; double dXf = 0, mXf = 0, cXf = 0;
  double gDry = 1, gWet = 0.8, gVol = 1, gSq = 0, gCr = 0, gMx = 0;
  Peak fHP, fF0, fBox; double fEnv = 0, fA = 0, fR = 0;
  int dlen = 1, dw = 0; std::vector<float> dL, dR; Smooth dtime;
  double eLpL = 0, eLpR = 0, eHpL = 0, eHpR = 0;
  double vb1 = 0, vb2 = 0, vdc1 = 0, vdc2 = 0, vLP = 0, sLP = 0, sLPc = 0;
  double sGain = 1, sVs = 0, sDs = 0, sDi = 0, sCd = 1, sBus = 1, sMod = 0, sMv = 0, lowS = 0, highS = 0;
  OS4 osL, osR; Slot slots[2]; Shelf lowSh4, highSh4;
  double cvbA = 0, cvbR = 0, cvdcC = 0;
  OTT ottV, ottE; double ob[2] = {0, 0};
  SVF xo[2]; double phs[2][6] = {}, phFb[2] = {0, 0}, phA[2] = {0, 0}; SVF nt[2][3]; double mvPh = 0;
  double vhS = 35, ehS = 80, vdS = 260, edS = 600; SVF vh[4], eh[4], vd[4], ed[4];
  double kEnv = 0, vDuckDb = 0, eDuckDb = 0;
  double bl1 = 0, bl2 = 0, bh1 = 0, bh2 = 0, benv[3] = {0, 0, 0}, sh1 = 0, sh2 = 0, tFast = 0, tSlow = 0, dcL = 0, dcR = 0;
  LookLim ceilLim, clipLim; std::vector<float> clipDL, clipDR; int clipI = 0; double lo2[2] = {0, 0};
  uint32_t rng = 22222;
  double rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng / 4294967296.0) * 2 - 1; }

  // block constants
  double gT, mkHP1, dXfS, mXfS, cXfS, gC, nPreT, nPostT, ckLen, ckW, ckMf, ckLPc, vLPc, mvInc, mvT;
  double scale, rgain[8], fdnIn, dampC, lhpC, preT, lfoInc, modT, dTarget, eLpC, eHpC, fb, oA, oR, oG, oDepth, busT;
  double kA, kR, dDown, dUp, dThr, vRatio, eRatio, dryG, wetG, cdT, c1, c2, cs, eAs[3], eR, tF, tS, sqT, crT, mxT, volT, dcC;

  void clearScalars() {
    for (auto& v : voices) v = Voice();
    hpIn = mLP = redShow = osRed = 0; pw = 0; vAlive = 0; for (int j = 0; j < 8; j++) { lph[j] = 0; fdn[j].w = 0; fdn[j].lp = fdn[j].hp = 0; }
    ckAge = 1e9; ckVel = ckPh = ckN = ckL1 = ckL2 = 0; ckBP.clear(); notch.clear(); notchPre.clear(); nfS = 300; nPreG = nPostG = 0;
    slot = 0; curType = -1; oldType = 0; curMv = -1; oldMv = 0; curCm = -1; oldCm = 0; dXf = mXf = cXf = 0;
    gDry = 1; gWet = 0.8; gVol = 1; gSq = gCr = gMx = 0; fHP.clear(); fF0.clear(); fBox.clear(); fEnv = 0;
    dw = 0; eLpL = eLpR = eHpL = eHpR = 0; vb1 = vb2 = vdc1 = vdc2 = vLP = sLP = 0;
    sGain = 1; sVs = sDs = sDi = 0; sCd = 1; sBus = 1; sMod = sMv = lowS = highS = 0;
    ottV.clear(); ottE.clear();
    for (int c = 0; c < 2; c++) { xo[c].clear(); phFb[c] = 0; for (int j = 0; j < 6; j++) phs[c][j] = 0; for (int j = 0; j < 3; j++) nt[c][j].clear(); }
    mvPh = 0; vhS = 35; ehS = 80; vdS = 260; edS = 600; for (int j = 0; j < 4; j++) { vh[j].clear(); eh[j].clear(); vd[j].clear(); ed[j].clear(); }
    kEnv = vDuckDb = eDuckDb = 0; bl1 = bl2 = bh1 = bh2 = 0; for (int j = 0; j < 3; j++) benv[j] = 0; sh1 = sh2 = tFast = tSlow = dcL = dcR = 0;
    clipI = 0; osL.clear(); osR.clear(); for (auto& s : slots) { s.os.clear(); s.lo.clear(); s.hi.clear(); } lowSh4.clear(); highSh4.clear();
  }

  // safety net: if anything ever goes non-finite, zero every state (no allocation)
  void reset() {
    for (int j = 0; j < 8; j++) std::fill(fdn[j].buf.begin(), fdn[j].buf.end(), 0.f);
    for (auto& a : ap) a.clear();
    std::fill(pre.begin(), pre.end(), 0.f); std::fill(dL.begin(), dL.end(), 0.f); std::fill(dR.begin(), dR.end(), 0.f);
    std::fill(clipDL.begin(), clipDL.end(), 0.f); std::fill(clipDR.begin(), clipDR.end(), 0.f);
    ceilLim.clear(); clipLim.clear();
    double keepPre = preS.v, keepDt = dtime.v; double keepLen[8]; for (int j = 0; j < 8; j++) keepLen[j] = rLen[j].v;
    clearScalars();
    preS.v = std::isfinite(keepPre) ? keepPre : 0.02 * sr; dtime.v = std::isfinite(keepDt) ? keepDt : 0.2 * sr;
    for (int j = 0; j < 8; j++) rLen[j].v = std::isfinite(keepLen[j]) ? keepLen[j] : fdnBase[j];
  }

  double synth(Voice& v) {
    double t = v.age / sr, L = v.kL;
    if (t > L && !v.fading) { v.fading = true; v.fade = 1; }
    double e = t < v.ksw ? std::pow(1 - t / v.ksw, v.kcv) : 0;
    v.ph += v.kf0 * (1 + v.kpu * e) * std::pow(2.0, -v.kdr * t / L) / sr; if (v.ph > 1) v.ph -= 1;
    double s = std::sin(v.ph * TAU);
    if (v.ksh > 0.001) { double tri = 2 / PI * std::asin(s); s = s * (1 - v.ksh) + std::sin(tri * 1.4) / std::sin(1.4) * v.ksh; }
    double att = t < v.kat ? std::pow(std::sin(PI / 2 * t / v.kat), 2) : 1;
    double amp = std::exp(-6.9 * std::pow(t / L, 1.15)) * att;
    v.n1 += (rnd() - v.n1) * bodyC; v.n2 += (v.n1 - v.n2) * bodyC;
    double x = tanhf_((s * amp + v.n2 * 9 * v.kbo * std::exp(-t / 0.07)) * 1.5) / TANH15;
    v.l1 += (x - v.l1) * v.kc; v.l2 += (v.l1 - v.l2) * v.kc; x = v.l2;
    double fg = 1;
    if (v.fading) { v.fade -= 1 / (0.012 * sr); if (v.fade <= 0) { v.on = false; return 0; } fg = 0.5 - 0.5 * std::cos(PI * v.fade); }
    v.age++;
    return x * 0.72 * v.vel * fg;
  }
  double voice(Voice& v) { return v.on ? synth(v) : 0; }

  double sineCore(double v) {
    v *= 0.8; double r = std::abs(v) - 1; if (r > osRed) osRed = r;
    double c = v > 1.5707963 ? 1.5707963 : v < -1.5707963 ? -1.5707963 : v; return std::sin(c);
  }
  double analogCore(double v) {
    v *= 1.3; double a = std::abs(v), r = a - 1; if (r > osRed) osRed = r;
    double a4 = a * a * a * a; return v / std::pow(1 + a4 * a4, 0.125) * 0.95;
  }
  double valveCore(double u) {
    vb1 += (std::max(0.0, u) - vb1) * (u > vb1 ? cvbA : cvbR);
    double a = triode(u * 1.2 - vb1 * 0.4);
    vdc1 += (a - vdc1) * cvdcC; a -= vdc1;
    a = highSh4.run(lowSh4.run(a * 1.1));
    vb2 += (std::max(0.0, a) - vb2) * (a > vb2 ? cvbA : cvbR);
    double b2 = triode(a * 0.9 - vb2 * 0.3), r = std::abs(a) * 0.9 - std::abs(b2); if (r > osRed) osRed = r;
    vdc2 += (b2 - vdc2) * cvdcC; return (b2 - vdc2) * 0.72;
  }

  // 0 bypass: the saturation circuit is skipped (only the 13 Hz coupling high-pass, inaudible)
  double driveOf(int type, double hx, Slot& sl, double g) {
    if (type == 2) { double m = sl.os.run(sl.hi.run(sl.lo.run(hx * g)), [this](double v) { return sineCore(v); }); sLP += (m - sLP) * sLPc; return sLP; }
    if (type == 3) return sl.os.run(sl.hi.run(sl.lo.run(hx * g)), [this](double v) { return analogCore(v); });
    if (type == 1) { double b2 = sl.os.run(hx * g, [this](double v) { return valveCore(v); }); vLP += (b2 - vLP) * vLPc; return vLP; }
    return hx;
  }

  double finish(double x) {
    x = fBox.run(fF0.run(fHP.run(x)));
    double a = std::abs(x);
    fEnv += (a - fEnv) * (a > fEnv ? fA : fR);
    double dbv = 20 * std::log10(fEnv + 1e-9), gr = dbv > -14 ? (dbv + 14) * (2.0 / 3) : 0;
    x *= dbToG(5 - gr);
    double ax = std::abs(x);
    return ax > 0.5 ? sgn(x) * (0.5 + 0.39 * tanhf_((ax - 0.5) / 0.39)) : x;
  }

  double hp2(SVF& a, SVF& b, double x) { a.run(x); b.run(a.hp); return b.hp; }

  double notchHi(int c, double hi, double ph, double amt) {
    double y = hi;
    for (int j = 0; j < 3; j++) {
      double l = 0.5 - 0.5 * std::cos(ph * (1 + j * 0.37) + j * 2.1);
      nt[c][j].set(300 * std::pow(2.0, 0.4 + j * 1.2 + l * 2.6 * amt), 1.2 + amt * 2, sr);
      nt[c][j].run(y); y = nt[c][j].lp + nt[c][j].hp;
    }
    return hi + (y - hi) * std::min(1.0, amt * 1.5);
  }
  double phaserHi(int c, double hi, double ph, double amt) {
    double lfo = 0.5 - 0.5 * std::cos(ph), f = 300 * std::pow(16.0, lfo * (0.3 + 0.7 * amt));
    double t = std::tan(PI * std::min(f, sr * 0.45) / sr); phA[c] = (t - 1) / (t + 1);
    double a = phA[c], v = hi + phFb[c] * 0.55 * amt;
    for (int j = 0; j < 6; j++) { double o = a * v + phs[c][j]; phs[c][j] = v - a * o; v = o; }
    phFb[c] = v;
    return (hi + v * std::min(1.0, amt * 1.4)) * 0.7;
  }
  double drift(int c, double x, double ph, double amt, int mode, int oMode, double w) {
    xo[c].run(x); double lo = xo[c].lp, hi = x - lo;
    double y = mode == 1 ? notchHi(c, hi, ph, amt) : phaserHi(c, hi, ph, amt);
    if (w > 0) { double yo = oMode == 1 ? notchHi(c, hi, ph, amt) : phaserHi(c, hi, ph, amt); y = y * (1 - w) + yo * w; }
    return lo + y;
  }

  void blockSetup() {
    gT = dbToG(clampd(p.gain, 0, 1) * 12);
    mkHP1 = 1 - std::exp(-TAU * 13 / sr);
    double kf0 = 440 * std::pow(2.0, (std::round(p.kpitch) - 69) / 12);
    fHP.set(1, 25, 0.7, 0, sr); fF0.set(0, kf0, 1.2, 2, sr); fBox.set(0, 320, 1, -3.5, sr);
    fA = 1 - std::exp(-1 / (0.012 * sr)); fR = 1 - std::exp(-1 / (0.14 * sr));
    lowS += (p.low - lowS) * 0.15; highS += (p.high - highS) * 0.15;
    double lq = std::round(lowS * 20) / 20, hq = std::round(highS * 20) / 20;
    for (auto& sl : slots) { sl.lo.set(true, 80, lq, sr); sl.hi.set(false, 12000, hq, sr); }
    int dt = std::clamp(p.dtype, 0, 3), mv = p.mvmode ? 1 : 0, cm = p.cmode ? 1 : 0;
    if (dt != curType) { if (curType >= 0) { oldType = curType; dXf = 1; slot = 1 - slot; } curType = dt; }
    if (mv != curMv) { if (curMv >= 0) { oldMv = curMv; mXf = 1; } curMv = mv; }
    if (cm != curCm) { if (curCm >= 0) { oldCm = curCm; cXf = 1; } curCm = cm; }
    dXfS = 1 / (0.02 * sr); mXfS = 1 / (0.04 * sr); cXfS = 1 / (0.02 * sr);
    gC = 1 - std::exp(-1 / (0.01 * sr));
    nfS += (clampd(p.nfreq, 200, 450) - nfS) * 0.25;
    notch.set(nfS, 2.5, sr); notchPre.set(nfS, 2.5, sr);
    double nCut = p.ndepth > 0.1 ? 1 - dbToG(-clampd(p.ndepth, 0, 30)) : 0; nPreT = p.npos == 0 ? nCut : 0; nPostT = p.npos == 0 ? 0 : nCut;
    double ctn = clampd(p.ctone, 800, 5000), ctK = std::log(ctn / 800) / std::log(5000.0 / 800);
    ckBP.set(ctn, 0.8, sr);
    ckLen = 0.04 * sr; ckW = (1.4 - 0.9 * ctK) * 0.001 * sr; ckMf = ctn * 0.3; ckLPc = 1 - std::exp(-TAU * std::min(9000.0, ctn * 2) / sr);
    lowSh4.set(true, 80, lq, S4); highSh4.set(false, 12000, hq, S4);
    cvbA = 1 - std::exp(-1 / (0.002 * S4)); cvbR = 1 - std::exp(-1 / (0.04 * S4));
    vLPc = 1 - std::exp(-TAU * 5000 / sr); cvdcC = 1 - std::exp(-TAU * 15 / S4);
    mvInc = TAU * clampd(p.mvrate, 0.01, 4) / sr; mvT = clampd(p.mvamt, 0, 1);
    xo[0].set(300, 0.5, sr); xo[1].set(300, 0.5, sr);
    scale = 1 + p.vsize * 1.5; double rt60 = 1.5 + p.vsize * p.vsize * 18, gAvg = 0;
    for (int j = 0; j < 8; j++) { rgain[j] = std::pow(10.0, -3 * (fdnBase[j] * scale) / (rt60 * sr)); gAvg += rgain[j] / 8; }
    fdnIn = 0.42 * std::sqrt(std::max(0.02, 1 - gAvg * gAvg)) / 0.5;
    dampC = 1 - std::exp(-TAU * lerp(14000, 7000, p.vsize) / sr); lhpC = 1 - std::exp(-TAU * 120 / sr);
    preT = clampd(p.vpre * sr, 1, (double)pre.size() - 4);
    lfoInc = TAU * 0.11 / sr; modT = 0.004 * sr * clampd(p.vmod, 0, 1);
    double beat = 60 / std::max(20.0, p.bpm);
    dTarget = clampd(DIVS[std::clamp(p.ddiv, 0, 4)] * 4 * beat * sr, 32, dlen - 8);
    eLpC = 1 - std::exp(-TAU * clampd(p.edark * 1.5, 200, 9000) / sr); eHpC = 1 - std::exp(-TAU * clampd(p.ehpf, 20, 1000) / sr);
    fb = clampd(p.dfb, 0, 0.95);
    auto glide = [](double s, double t) { return std::exp(std::log(s) + (std::log(t) - std::log(s)) * 0.25); };
    vhS = glide(vhS, clampd(p.vhpf, 20, 4000)); ehS = glide(ehS, clampd(p.ehpf, 20, 1000));
    vdS = glide(vdS, clampd(p.vdark, 50, 16000)); edS = glide(edS, clampd(p.edark, 50, 8000));
    for (int j = 0; j < 4; j++) { vh[j].set(vhS, Q4[j & 1], sr); eh[j].set(ehS, Q4[j & 1], sr); vd[j].set(vdS, Q4[j & 1], sr); ed[j].set(edS, Q4[j & 1], sr); }
    double tK = std::pow(4.0, (clampd(p.otime, 0, 1) - 0.5) * 2);
    oA = 1 - std::exp(-1 / (0.010 * tK * sr)); oR = 1 - std::exp(-1 / (0.15 * tK * sr)); oG = 1 - std::exp(-1 / (0.005 * sr));
    oDepth = clampd(p.ott, 0, 1); busT = dbToG(9 + 2 * oDepth);
    kA = 1 - std::exp(-1 / (0.001 * sr)); kR = std::exp(-1 / (0.03 * sr));
    dDown = 1 - std::exp(-1 / (0.006 * sr)); dUp = 1 - std::exp(-1 / (std::max(0.02, p.drel) / 3 * sr));
    dThr = -24; vRatio = p.vduck * 1.2; eRatio = p.educk * 1.2;
    dryG = p.listen == 2 ? 0 : p.dry; wetG = p.listen == 1 ? 0 : p.wet;
    cdT = dbToG(p.clip * 15);
    c1 = 1 - std::exp(-TAU * 200 / sr); c2 = 1 - std::exp(-TAU * 2500 / sr); cs = 1 - std::exp(-TAU * 150 / sr);
    eAs[0] = 1 - std::exp(-1 / (0.015 * sr)); eAs[1] = 1 - std::exp(-1 / (0.006 * sr)); eAs[2] = 1 - std::exp(-1 / (0.003 * sr));
    eR = 1 - std::exp(-1 / (0.12 * sr)); tF = 1 - std::exp(-1 / (0.002 * sr)); tS = 1 - std::exp(-1 / (0.05 * sr));
    sqT = p.sealOn ? p.squash : 0; crT = p.sealOn ? p.crush : 0; mxT = p.sealOn ? p.smix : 0;
    volT = dbToG(p.vol); dcC = 1 - std::exp(-TAU * 5 / sr);
  }

  void tick(double* out) {
    sGain += (gT - sGain) * gC; sVs += (p.vsend - sVs) * gC; sDs += (p.dsend - sDs) * gC;
    sDi += (p.dinto - sDi) * gC; sCd += (cdT - sCd) * gC; sBus += (busT - sBus) * gC; sMod += (modT - sMod) * gC; sMv += (mvT - sMv) * gC;
    double modD = sMod, mvAmt = sMv, busG = sBus, cDrive = sCd;
    double x = finish(voice(voices[0]) + voice(voices[1]));

    nPreG += (nPreT - nPreG) * gC; nPostG += (nPostT - nPostG) * gC;
    notchPre.run(x);
    double xn = x - (x - notchPre.lp - notchPre.hp) * nPreG;
    hpIn += (xn - hpIn) * mkHP1;
    double hx = xn - hpIn;
    double m = driveOf(curType, hx, slots[slot], sGain);
    if (dXf > 0) {
      double mo = driveOf(oldType, hx, slots[1 - slot], sGain);
      double w = 0.5 - 0.5 * std::cos(PI * dXf); m = m * (1 - w) + mo * w;
      dXf = std::max(0.0, dXf - dXfS);
    }
    if (osRed > redShow) redShow = osRed;
    osRed = 0;
    notch.run(m); m = m - (m - notch.lp - notch.hp) * nPostG;

    double ck = 0;
    if (ckVel > 0.001 && ckAge < ckLen) {
      double a = ckAge, t = a / sr;
      double imp = a < ckW ? std::sin(PI * a / ckW) : 0;
      ckN += (rnd() - ckN) * 0.7;
      double slap = ckBP.run(ckN) * std::exp(-t / 0.0012) * 0.7;
      ckPh += ckMf * (1 + 0.3 * std::exp(-t / 0.002)) / sr;
      double ring = std::sin(ckPh * TAU) * std::exp(-t / 0.006);
      double c = imp * 1.0 + slap + ring * 0.5;
      ckL1 += (c - ckL1) * ckLPc; ckL2 += (ckL1 - ckL2) * ckLPc; c = ckL2;
      ck = c * ckVel * 0.25;
    }
    ckAge++;
    double dry = m * 0.5 + ck;

    double ak = std::abs(dry);
    kEnv = ak > kEnv ? kEnv + (ak - kEnv) * kA : ak + (kEnv - ak) * kR;

    // echo
    double dtv = dtime.tick(dTarget);
    double rp = dw - dtv; while (rp < 0) rp += dlen;
    int ri = (int)std::floor(rp) % dlen; double rf = rp - std::floor(rp); int rj = (ri + 1) % dlen;
    double oL = dL[ri] + (dL[rj] - dL[ri]) * rf, oR = dR[ri] + (dR[rj] - dR[ri]) * rf;
    eLpL += (oL - eLpL) * eLpC; eHpL += (eLpL - eHpL) * eHpC;
    eLpR += (oR - eLpR) * eLpC; eHpR += (eLpR - eHpR) * eHpC;
    double fL = safe((eLpL - eHpL) * fb), fR = safe((eLpR - eHpR) * fb);
    dL[dw] = (float)(dry * sDs + fR); dR[dw] = (float)fL;
    dw = (dw + 1) % dlen;
    double echoL = oL, echoR = oR;

    // haze
    double vL = 0, vR = 0;
    double vin = dry * sVs + (echoL + echoR) * 0.5 * sDi;
    if (std::abs(vin) > 1e-7 || vAlive > 0) {
      int plen = (int)pre.size();
      pre[pw] = (float)vin;
      double pd = preS.tick(preT);
      double pp = pw - pd; while (pp < 0) pp += plen;
      int pi = (int)std::floor(pp) % plen; double pf = pp - std::floor(pp); int pj = (pi + 1) % plen;
      double d = pre[pi] + (pre[pj] - pre[pi]) * pf;
      pw = (pw + 1) % plen;
      for (int j = 0; j < 4; j++) d = ap[j].run(d);
      double inp = d * fdnIn, o[8], sum = 0;
      for (int j = 0; j < 8; j++) {
        FdnLine& ln = fdn[j];
        double len = rLen[j].tick(fdnBase[j] * scale);
        double lp = lph[j] + lfoInc * (1 + j * 0.23); if (lp > TAU) lp -= TAU; lph[j] = lp;
        len += std::sin(lp + j * 1.7) * modD * (0.4 + 0.6 * ((j * 5) % 8) / 7.0);
        int bl = (int)ln.buf.size();
        double q = ln.w - len; while (q < 0) q += bl;
        int q0 = (int)std::floor(q); double qf = q - q0; int i1 = q0 % bl, i0 = (i1 + bl - 1) % bl, i2 = (i1 + 1) % bl, i3 = (i1 + 2) % bl;
        double ym = ln.buf[i0], y0 = ln.buf[i1], y1 = ln.buf[i2], y2 = ln.buf[i3];
        double h1 = 0.5 * (y1 - ym), h2 = ym - 2.5 * y0 + 2 * y1 - 0.5 * y2, h3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
        double y = ((h3 * qf + h2) * qf + h1) * qf + y0;
        ln.lp += (y - ln.lp) * dampC; y = ln.lp;
        ln.hp += (y - ln.hp) * lhpC; y -= ln.hp;
        o[j] = y; sum += y;
      }
      double hh = sum * 0.25, e = 0;
      for (int j = 0; j < 8; j++) {
        FdnLine& ln = fdn[j];
        ln.buf[ln.w] = (float)(((j & 1) ? inp : -inp) + (o[j] - hh) * rgain[j]);
        ln.w = (ln.w + 1) % (int)ln.buf.size();
        e += std::abs(o[j]);
      }
      vL = (o[0] - o[2] + o[4] - o[6] + o[1] * 0.3) * 0.9; vR = (o[1] - o[3] + o[5] - o[7] - o[0] * 0.3) * 0.9;
      vAlive = e > 1e-6 ? 1 : 0;
      if (mXf > 0) mXf = std::max(0.0, mXf - mXfS);
      if (mvAmt > 0.001) {
        mvPh += mvInc; if (mvPh > TAU * 100) mvPh -= TAU * 100;
        double mw = mXf > 0 ? 0.5 - 0.5 * std::cos(PI * mXf) : 0;
        vL = drift(0, vL, mvPh, mvAmt, curMv, oldMv, mw);
        vR = drift(1, vR, mvPh + PI / 2, mvAmt, curMv, oldMv, mw);
      }
    }

    double rL = vd[1].run(vd[0].run(hp2(vh[0], vh[1], vL))), rR = vd[3].run(vd[2].run(hp2(vh[2], vh[3], vR)));
    double xL = ed[1].run(ed[0].run(hp2(eh[0], eh[1], echoL * 0.7))), xR = ed[3].run(ed[2].run(hp2(eh[2], eh[3], echoR * 0.7)));
    if (oDepth > 0.001) {
      ottV.run(rL, rR, oDepth, oA, oR, oG, ob); rL = ob[0]; rR = ob[1];
      ottE.run(xL, xR, oDepth, oA, oR, oG, ob); xL = ob[0]; xR = ob[1];
    }
    rL *= busG; rR *= busG; xL *= busG; xR *= busG;
    double kdb = 20 * std::log10(kEnv + 1e-7) - dThr;
    double vT = kdb > 0 ? std::min(30.0, kdb * vRatio) : 0, eT = kdb > 0 ? std::min(30.0, kdb * eRatio) : 0;
    vDuckDb += (vT - vDuckDb) * (vT > vDuckDb ? dDown : dUp);
    eDuckDb += (eT - eDuckDb) * (eT > eDuckDb ? dDown : dUp);
    double vG = dbToG(-vDuckDb), eG = dbToG(-eDuckDb);
    double wL = rL * vG + xL * eG, wR = rR * vG + xR * eG;

    gDry += (dryG - gDry) * gC; gWet += (wetG - gWet) * gC;
    double sL = dry * gDry + wL * gWet, sR = dry * gDry + wR * gWet;
    sL *= cDrive; sR *= cDrive;
    clipLim.run(sL, sR, lo2); double lL = lo2[0], lR = lo2[1]; clipLim.red = 0;
    double qL = osL.run(sL, [](double v) { return soft(v); }), qR = osR.run(sR, [](double v) { return soft(v); });
    { int oq = clipI; double tL = clipDL[oq], tR = clipDR[oq]; clipDL[oq] = (float)qL; clipDR[oq] = (float)qR; clipI = (oq + 1) % (int)clipDL.size(); qL = tL; qR = tR; }
    if (cXf > 0) {
      double w = 0.5 - 0.5 * std::cos(PI * cXf);
      double nL = curCm == 1 ? lL : qL, nR = curCm == 1 ? lR : qR, ooL = curCm == 1 ? qL : lL, ooR = curCm == 1 ? qR : lR;
      sL = nL * (1 - w) + ooL * w; sR = nR * (1 - w) + ooR * w; cXf = std::max(0.0, cXf - cXfS);
    } else if (curCm == 1) { sL = lL; sR = lR; } else { sL = qL; sR = qR; }
    sL *= 0.5; sR *= 0.5;

    // bsbr seal
    gSq += (sqT - gSq) * gC; gCr += (crT - gCr) * gC; gMx += (mxT - gMx) * gC; gVol += (volT - gVol) * gC;
    double sq = gSq, cr = gCr, mx = gMx, vol = gVol;
    double mm = (sL + sR) * 0.5, sd = (sL - sR) * 0.5;
    sh1 += (sd - sh1) * cs; sh2 += (sh1 - sh2) * cs; sd -= sh2;
    bl1 += (mm - bl1) * c1; bl2 += (bl1 - bl2) * c1; double lo = bl2;
    bh1 += (mm - bh1) * c2; bh2 += (bh1 - bh2) * c2; double hi = mm - bh2, mid = mm - lo - hi;
    double bands[3] = {lo, mid, hi}, sum2 = 0;
    for (int j = 0; j < 3; j++) {
      double ab = std::abs(bands[j]);
      benv[j] += (ab - benv[j]) * (ab > benv[j] ? eAs[j] : eR);
      double dbv = 20 * std::log10(benv[j] + 1e-6), thr = -22, upT = -42, gdb = 0;
      if (dbv > thr) gdb -= (dbv - thr) * 0.75 * sq;
      else if (dbv > upT - 30) gdb += std::min(12.0, (thr - dbv) * 0.5) * sq * (dbv > upT ? 1 : std::max(0.0, 1 - (upT - dbv) / 30));
      sum2 += bands[j] * dbToG(gdb + sq * 4);
    }
    tFast += (std::abs(mm) - tFast) * tF; tSlow += (std::abs(mm) - tSlow) * tS;
    double trans = std::max(0.0, tFast - tSlow), drv = 1 + cr * 5;
    double wet = tanhf_(sum2 * drv * (1 + trans * cr * 6)) / std::sqrt(drv) * 1.2;
    double mOut = mm + (wet - mm) * mx;
    double oL2 = (mOut + sd) * vol * 2, oR2 = (mOut - sd) * vol * 2;
    dcL += (oL2 - dcL) * dcC; oL2 -= dcL; dcR += (oR2 - dcR) * dcC; oR2 -= dcR;
    ceilLim.run(oL2, oR2, lo2); oL2 = lo2[0]; oR2 = lo2[1]; ceilLim.red = 0;
    if (oL2 > 0.99 || oL2 < -0.99) oL2 = tanhf_(oL2);
    if (oR2 > 0.99 || oR2 < -0.99) oR2 = tanhf_(oR2);
    if (!std::isfinite(oL2) || !std::isfinite(oR2)) { oL2 = oR2 = 0; reset(); }
    out[0] = oL2; out[1] = oR2;
    double pk = std::max(std::abs(oL2), std::abs(oR2)); if (pk > peakOut) peakOut = (float)pk;
  }
};

} // namespace undertow
