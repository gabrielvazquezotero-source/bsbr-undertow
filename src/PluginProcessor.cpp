#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace undertow;

juce::String noteName(int n) {
  static const char* N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return juce::String(N[((n % 12) + 12) % 12]) + juce::String(n / 12 - 2); // Live naming: C3 = 60
}

// text shown for a value, by unit (same units as the Max for Live device)
juce::String formatValue(int i, float v) {
  const ParamDef& d = kParams[i];
  switch (d.unit) {
    case 2: return v >= 1000 ? juce::String(v / 1000, 2) + " s" : juce::String(v, v < 10 ? 2 : (v < 100 ? 1 : 0)) + " ms";
    case 3: return v >= 1000 ? juce::String(v / 1000, 2) + " kHz" : juce::String(v, v < 10 ? 2 : 0) + " Hz";
    case 4: return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB";
    case 5: return juce::String(juce::roundToInt(v)) + " %";
    case 7: { int st = juce::roundToInt(v); return st == 0 ? juce::String("0 st") : "-" + juce::String(st) + " st"; }
    case 8: { int n = juce::roundToInt(v); return noteName(n) + "  " + juce::String(440.0 * std::pow(2.0, (n - 69) / 12.0), 1) + " Hz"; }
    default: return juce::String(v, 2);
  }
}

juce::AudioProcessorValueTreeState::ParameterLayout UndertowProcessor::layout() {
  juce::AudioProcessorValueTreeState::ParameterLayout l;
  for (int i = 0; i < kNumParams; i++) {
    const ParamDef& d = kParams[i];
    juce::ParameterID id{d.id, 1};
    juce::String name = juce::String(d.label) + (juce::String(d.label) == "Duck" || juce::String(d.label) == "HPF" || juce::String(d.label) == "LPF" || juce::String(d.label) == "Fill" || juce::String(d.label) == "Time" ? " " + juce::String(d.section).toLowerCase() : "");
    if (d.kind == kToggle) l.add(std::make_unique<juce::AudioParameterBool>(id, name, d.def > 0.5));
    else if (d.kind == kTab) l.add(std::make_unique<juce::AudioParameterChoice>(id, name, juce::StringArray::fromTokens(d.choices, "|", ""), (int)d.def));
    else {
      juce::NormalisableRange<float> r((float)d.mn, (float)d.mx, d.integer ? 1.f : 0.f, (float)d.skew);
      l.add(std::make_unique<juce::AudioParameterFloat>(id, name, r, (float)d.def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction([i](float v, int) { return formatValue(i, v); })));
    }
  }
  return l;
}

UndertowProcessor::UndertowProcessor()
  : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "undertow", layout()) {
  for (int i = 0; i < kNumParams; i++) raw[i] = apvts.getRawParameterValue(kParams[i].id);
  for (auto& m : mSeal) m.store(0);
  apvts.state.setProperty("ink", true, nullptr);
  loadPreset(0);
}

void UndertowProcessor::setP(const char* id, double v) {
  if (auto* p = apvts.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)v)); p->endChangeGesture(); }
}

void UndertowProcessor::startFrom(int k) {
  if (k < 0 || k >= kNumKit) return;
  const KitKick& c = kKit[k];
  setP("kpitch", c.note); setP("kpunch", c.punch); setP("ksweep", c.sweep * 1000); setP("kcurve", c.curve * 100); setP("kshape", c.shape * 100);
  setP("klen", c.len * 1000); setP("kbody", c.body * 100); setP("kdrop", c.drop);
  kit = k;
}

void UndertowProcessor::squall() {
  auto rr = [&](double a, double b) { return a + (b - a) * rnd.nextDouble(); };
  auto rl = [&](double a, double b) { return a * std::pow(b / a, rnd.nextDouble()); };
  auto ch = [&](double p) { return rnd.nextDouble() < p; };
  setP("gain", rr(0.3, 0.9) * 12); setP("low", std::round(rr(-2, 6))); setP("high", std::round(rr(-10, 2)));
  setP("vsend", rr(20, 50)); setP("vsize", rr(40, 100)); setP("vmod", rr(20, 80)); setP("click", rr(0, 60)); setP("ctone", rl(900, 4000));
  setP("nfreq", rl(220, 420)); setP("ndepth", ch(0.4) ? 0 : rr(6, 18)); setP("vpre", ch(0.3) ? rr(30, 100) : rr(0, 30));
  setP("dsend", rr(25, 60)); setP("ddiv", rnd.nextInt(5)); setP("dfb", rr(25, 65)); setP("dinto", rr(10, 35));
  setP("vhpf", rl(500, 2000)); setP("ehpf", rl(25, 50)); setP("vdark", rl(6000, 14000)); setP("edark", rl(150, 500));
  setP("ott", rr(15, 60)); setP("otime", rr(30, 70)); setP("vduck", rr(60, 95)); setP("educk", rr(60, 100)); setP("drel", rl(100, 450));
  setP("kpitch", std::round(rr(27, 33))); setP("kpunch", rl(1.5, 4)); setP("ksweep", rl(60, 200)); setP("kcurve", rr(30, 75));
  setP("kshape", ch(0.4) ? 0 : rr(0, 40)); setP("klen", rl(400, 1400)); setP("kbody", ch(0.5) ? 0 : rr(0, 70)); setP("kdrop", ch(0.6) ? 0 : rr(0.5, 5));
  setP("dtype", 1 + rnd.nextInt(3)); setP("mvmode", rnd.nextInt(2)); setP("mvamt", ch(0.25) ? 0 : rr(20, 80)); setP("mvrate", rl(0.03, 0.6));
  kit = -1;
}

int UndertowProcessor::pullColumns(Engine::Col* dst, int maxN) {
  int w = eng.ringW.load(std::memory_order_acquire), n = 0;
  while (ringR != w && n < maxN) { dst[n++] = eng.ring[ringR]; ringR = (ringR + 1) % Engine::RING; }
  return n;
}

const juce::String UndertowProcessor::getProgramName(int i) {
  return i >= 0 && i < kNumPresets ? juce::String::fromUTF8(kPresets[i].name) : juce::String();
}

void UndertowProcessor::loadPreset(int i) {
  if (i < 0 || i >= kNumPresets) return;
  cur = i; kit = -1;
  for (int k = 0; k < kNumParams; k++)
    if (auto* p = par(k)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float)kPresets[i].v[k])); p->endChangeGesture(); }
}

void UndertowProcessor::prepareToPlay(double sr, int bs) {
  eng.prepare(sr);
  L.assign((size_t)std::max(bs, 4096), 0.f); R = L;
  setLatencySamples(eng.latencySamples());
  sampleRateNow = sr;
  eng.colLen = std::max(16, (int)std::round(sr * 4.0 / std::max(100, colsAcross.load())));
  ringR = eng.ringW.load();
}

static void amax(std::atomic<float>& a, float v) { float c = a.load(); while (v > c && !a.compare_exchange_weak(c, v)) {} }

void UndertowProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals nd;
  const int n = buf.getNumSamples();
  if ((int)L.size() < n) { L.assign((size_t)n, 0.f); R = L; } // only if the host breaks its promise
  for (int i = 0; i < kNumParams; i++) applyParam(eng.p, i, raw[i]->load());
  double bpm = 120;
  if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
  eng.p.bpm = bpm;
  eng.colLen = std::max(16, (int)std::round(getSampleRate() * 4.0 / std::max(100, colsAcross.load())));
  int done = 0;
  auto render = [&](int upto) { if (upto > done) { eng.process(L.data() + done, R.data() + done, upto - done); done = upto; } };
  float ph = pendingHit.exchange(-1.f);
  if (ph >= 0) eng.trigger(ph);
  for (const auto m : midi) {
    auto msg = m.getMessage();
    render(juce::jlimit(0, n, m.samplePosition));
    if (msg.isNoteOn()) eng.trigger(std::min(1.0, msg.getFloatVelocity() * 127.0 / 100.0)); // velocity 100 or more = full hit
    else if (msg.isAllNotesOff() || msg.isAllSoundOff()) eng.allOff();
  }
  render(n);
  buf.copyFrom(0, 0, L.data(), n);
  if (buf.getNumChannels() > 1) buf.copyFrom(1, 0, R.data(), n);
  amax(meter, eng.peakOut); eng.peakOut = 0;
  amax(mDrv, eng.mDrv); amax(mDuckV, eng.mDuckV); amax(mDuckE, eng.mDuckE); amax(mClip, eng.mClip); amax(mLim, eng.mLim);
  amax(mOttD, eng.ottDown()); amax(mOttU, eng.ottUp());
  for (int j = 0; j < 3; j++) { amax(mSeal[j], eng.mSeal[j]); eng.mSeal[j] = 0; }
  eng.mDrv = eng.mDuckV = eng.mDuckE = eng.mClip = eng.mLim = 0;
  voicePos.store((float)eng.voicePos);
  if (eng.hits != hits.load()) { hitVel.store(eng.lastVel); hits.store(eng.hits); }
}

void UndertowProcessor::getStateInformation(juce::MemoryBlock& d) {
  auto s = apvts.copyState(); s.setProperty("program", cur.load(), nullptr);
  if (auto x = s.createXml()) copyXmlToBinary(*x, d);
}
void UndertowProcessor::setStateInformation(const void* data, int size) {
  if (auto x = getXmlFromBinary(data, size)) {
    auto s = juce::ValueTree::fromXml(*x);
    if (s.isValid() && s.hasType(apvts.state.getType())) {
      apvts.replaceState(s); cur = (int)s.getProperty("program", 0);
      if (!apvts.state.hasProperty("ink")) apvts.state.setProperty("ink", true, nullptr);
    }
  }
}
juce::AudioProcessorEditor* UndertowProcessor::createEditor() { return new UndertowEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UndertowProcessor(); }
