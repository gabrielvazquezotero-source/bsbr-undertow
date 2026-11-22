#include "PluginProcessor.h"
#include "PluginEditor.h"
using namespace undertow;

static juce::String noteName(int n) {
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
  loadPreset(0);
}

const juce::String UndertowProcessor::getProgramName(int i) {
  return i >= 0 && i < kNumPresets ? juce::String::fromUTF8(kPresets[i].name) : juce::String();
}

void UndertowProcessor::loadPreset(int i) {
  if (i < 0 || i >= kNumPresets) return;
  cur = i;
  for (int k = 0; k < kNumParams; k++)
    if (auto* p = par(k)) p->setValueNotifyingHost(p->convertTo0to1((float)kPresets[i].v[k]));
}

void UndertowProcessor::prepareToPlay(double sr, int bs) {
  eng.prepare(sr);
  L.assign((size_t)std::max(bs, 4096), 0.f); R = L;
  setLatencySamples(eng.latencySamples());
}

void UndertowProcessor::processBlock(juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi) {
  juce::ScopedNoDenormals nd;
  const int n = buf.getNumSamples();
  if ((int)L.size() < n) { L.assign((size_t)n, 0.f); R = L; } // only if the host breaks its promise
  for (int i = 0; i < kNumParams; i++) applyParam(eng.p, i, raw[i]->load());
  double bpm = 120;
  if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
  eng.p.bpm = bpm;
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
  meter.store(eng.peakOut); eng.peakOut = 0;
  if (eng.hits != hits.load()) { hitVel.store(eng.lastVel); hits.store(eng.hits); }
}

void UndertowProcessor::getStateInformation(juce::MemoryBlock& d) {
  auto s = apvts.copyState(); s.setProperty("program", cur.load(), nullptr);
  if (auto x = s.createXml()) copyXmlToBinary(*x, d);
}
void UndertowProcessor::setStateInformation(const void* data, int size) {
  if (auto x = getXmlFromBinary(data, size)) {
    auto s = juce::ValueTree::fromXml(*x);
    if (s.isValid() && s.hasType(apvts.state.getType())) { apvts.replaceState(s); cur = (int)s.getProperty("program", 0); }
  }
}
juce::AudioProcessorEditor* UndertowProcessor::createEditor() { return new UndertowEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new UndertowProcessor(); }
