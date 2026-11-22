#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "undertow_params.h"

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
  void audition(float vel = 1.f) { pendingHit.store(vel); }
  juce::AudioProcessorValueTreeState apvts;
  std::atomic<int> cur{0};
  std::atomic<float> meter{0};
  std::atomic<int> hits{0};
  std::atomic<float> hitVel{0};
  static juce::AudioProcessorValueTreeState::ParameterLayout layout();
  juce::RangedAudioParameter* par(int i) { return apvts.getParameter(undertow::kParams[i].id); }

private:
  undertow::Engine eng;
  std::vector<float> L, R;
  std::atomic<float>* raw[undertow::kNumParams] = {};
  std::atomic<float> pendingHit{-1.f};
};
