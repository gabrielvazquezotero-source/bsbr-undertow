#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "undertow_params.h"

// starting kicks (as in the web prototype): name, family, note, punch, sweep s, curve, shape, length s, body, drop st
struct KitKick { const char* name; const char* family; double note, punch, sweep, curve, shape, len, body, drop; };
static const KitKick kKit[] = {
  {"Short", "909", 32, 3.2, 0.09, 0.6, 0.35, 0.45, 0, 0}, {"Long", "909", 31, 3, 0.1, 0.55, 0.3, 1.0, 0, 0}, {"Dirt", "909", 31, 3.5, 0.08, 0.65, 0.45, 0.6, 0.15, 0},
  {"Boom", "808", 30, 1.5, 0.12, 0.4, 0, 1.6, 0, 1}, {"Tight", "808", 31, 1.8, 0.08, 0.5, 0, 0.5, 0, 0}, {"Dive", "808", 31, 2.2, 0.1, 0.45, 0, 1.3, 0, 5},
  {"Hypno", "deep", 30, 2.6, 0.09, 0.6, 0.1, 0.8, 0.1, 0}, {"Tunnel", "deep", 28, 3, 0.16, 0.45, 0, 1.2, 0.6, 0}, {"Felt", "deep", 31, 1.8, 0.12, 0.4, 0, 0.6, 0.2, 0},
  {"Tape", "deep", 31, 3.5, 0.1, 0.6, 0.25, 0.7, 0.2, 0}, {"Iron", "deep", 32, 3, 0.08, 0.7, 0.3, 0.6, 0, 0}, {"Dub", "deep", 27, 2, 0.24, 0.35, 0.05, 1.4, 0.1, 2},
  {"Fog", "deep", 29, 2.4, 0.14, 0.45, 0, 1.1, 0.9, 1}, {"Abyss", "deep", 26, 2.8, 0.2, 0.4, 0, 1.6, 0.35, 3}};
static constexpr int kNumKit = 14;

class UndertowProcessor : public juce::AudioProcessor {
public:
  UndertowProcessor();
  void prepareToPlay(double sr, int bs) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return "undertow"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 20.0; }
  int getNumPrograms() override { return undertow::kNumPresets; }
  int getCurrentProgram() override { return cur; }
  void setCurrentProgram(int i) override { loadPreset(i); }
  const juce::String getProgramName(int i) override;
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  void loadPreset(int i);
  void squall();          // random new kick and sound within musical limits (mix and seal kept)
  void startFrom(int k);  // one of the 14 starting kicks
  void audition(float vel = 1.f) { pendingHit.store(vel); }
  void setP(const char* id, double v);
  double getP(const char* id) const { return apvts.getRawParameterValue(id)->load(); }

  juce::AudioProcessorValueTreeState apvts;
  std::atomic<int> cur{0};
  std::atomic<int> kit{-1};  // starting kick last chosen, -1 none / squall
  // meters (max since last read; the editor exchanges them to 0)
  std::atomic<float> meter{0}, mDrv{0}, mDuckV{0}, mDuckE{0}, mOttD{0}, mOttU{0}, mClip{0}, mLim{0}, mSeal[3];
  std::atomic<int> hits{0};
  std::atomic<float> hitVel{0}, voicePos{-1};
  std::atomic<double> sampleRateNow{48000};
  // live view columns (4 s across the view)
  int pullColumns(undertow::Engine::Col* dst, int maxN);
  void setColumnsAcross(int n) { colsAcross.store(n); }
  static juce::AudioProcessorValueTreeState::ParameterLayout layout();
  juce::RangedAudioParameter* par(int i) { return apvts.getParameter(undertow::kParams[i].id); }

private:
  undertow::Engine eng;
  std::vector<float> L, R;
  std::atomic<float>* raw[undertow::kNumParams] = {};
  std::atomic<float> pendingHit{-1.f};
  std::atomic<int> colsAcross{600};
  int ringR = 0;
  juce::Random rnd;
};

juce::String formatValue(int paramIndex, float v);
