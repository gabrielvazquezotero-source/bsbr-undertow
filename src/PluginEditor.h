#pragma once
#include "PluginProcessor.h"

namespace ui {
struct Theme {
  juce::Colour bg, fg, dim, mut;
  static Theme paper() { return {juce::Colour(0xfff2f2ee), juce::Colour(0xff0c0c0c), juce::Colour(0xffcfcfc8), juce::Colour(0xff55554f)}; }
  static Theme ink() { return {juce::Colour(0xff0c0c0c), juce::Colour(0xfff2f2ee), juce::Colour(0xff3a3a37), juce::Colour(0xffa8a8a0)}; }
};

juce::Font mono(float height, float kerning = 0.0f);

class LnF : public juce::LookAndFeel_V4 {
public:
  Theme t = Theme::ink();
  void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
  void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
  int getSliderThumbRadius(juce::Slider&) override { return 6; }
  juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(juce::ComboBox&, juce::Label&) override;
  void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
  void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                         const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon, const juce::Colour* textColour) override;
  void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
  void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight, int& w, int& h) override;
};

// one rope line: label, dotted track with a ring thumb, value
class RopeRow : public juce::Component {
public:
  RopeRow(juce::RangedAudioParameter&, const juce::String& label, bool bipolar, const Theme& t, int labelW);
  void paint(juce::Graphics&) override;
  void resized() override;
private:
  const Theme& theme;
  juce::RangedAudioParameter& param;
  juce::String label;
  int labelW;
  juce::Slider slider;
  juce::SliderParameterAttachment att;
};

// pills bound to a choice parameter
class ChoicePills : public juce::Component {
public:
  ChoicePills(juce::RangedAudioParameter&, const juce::StringArray& labels, float fontSize);
  void resized() override;
  int preferredWidth() const;
private:
  juce::OwnedArray<juce::TextButton> buttons;
  float fs;
  juce::ParameterAttachment att;
};

// the live view: the kick on the surface, haze and echo as the current below, a shock wave on every hit
class Tide : public juce::Component {
public:
  static constexpr int W = 596, H = 196;
  Tide(UndertowProcessor&, const Theme&);
  void tick();
  void paint(juce::Graphics&) override;
private:
  UndertowProcessor& proc;
  const Theme& theme;
  juce::Image pix{juce::Image::ARGB, W, H, true};
  std::vector<undertow::Engine::Col> cols, tmp;
  int head = 0, lastHits = 0;
  struct Shock { double t0; float v; };
  std::vector<Shock> shocks;
  float outNow = 0;
};

// the kick as drawn by the synth, with the playhead of the hit that is sounding
class Shape : public juce::Component {
public:
  static constexpr int W = 596, H = 42;
  Shape(UndertowProcessor&, const Theme&);
  void tick();
  void paint(juce::Graphics&) override;
private:
  UndertowProcessor& proc;
  const Theme& theme;
  juce::Image pix{juce::Image::ARGB, W, H, true};
  std::vector<float> env;
  juce::String key;
  float len = 1, sweepAt = 0.1f, pos = -1;
  void compute();
};

struct Read { float drv = 0, dv = 0, de = 0, od = 0, ou = 0, clip = 0, out = 0, seal[3] = {0, 0, 0}; };

class Meters : public juce::Component {
public:
  Meters(const Theme& t, bool sealBands) : theme(t), seal(sealBands) {}
  Read r;
  void paint(juce::Graphics&) override;
private:
  const Theme& theme;
  bool seal;
};

class Content : public juce::Component, private juce::Timer {
public:
  Content(UndertowProcessor&, LnF&);
  ~Content() override;
  void paint(juce::Graphics&) override;
  void resized() override;
  void applyTheme();
  void tickNow() { timerCallback(); }
  static constexpr int W = 1200, H = 784;
private:
  void timerCallback() override;
  UndertowProcessor& proc;
  LnF& lnf;
  Theme& theme;
  juce::Image logo;
  juce::OwnedArray<RopeRow> ropes;
  juce::OwnedArray<ChoicePills> pills;
  std::map<juce::String, int> ropeIdx, pillIdx;
  juce::TextButton prev{"<"}, next{">"}, weather, hit{"HIT"}, squall{"SQUALL"}, invert{"INVERT"}, start{"START FROM"}, sealOn{"SEAL"};
  std::unique_ptr<juce::ButtonParameterAttachment> sealAtt;
  Tide tide;
  Shape shape;
  Meters reading, sealMeters;
  Read held;
  int lastHits = 0; double flashT = 0;
  RopeRow& rope(const char* id);
  ChoicePills& pill(const char* id);
  void addRope(const char* id, const char* label, bool bip = false, int labelW = 74);
  void addPills(const char* id, juce::StringArray labels, float fs = 9);
  void showWeather();
  void showStart();
  void place(const char* id, int x, int y, int w);
  void placePills(const char* id, int x, int y, int h = 20);
};
} // namespace ui

class UndertowEditor : public juce::AudioProcessorEditor, private juce::ValueTree::Listener {
public:
  explicit UndertowEditor(UndertowProcessor&);
  ~UndertowEditor() override;
  void paint(juce::Graphics&) override {}
  void resized() override;
  void tickForTest() { content->tickNow(); }
private:
  void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
  UndertowProcessor& proc;
  ui::LnF lnf;
  std::unique_ptr<ui::Content> content;
};
