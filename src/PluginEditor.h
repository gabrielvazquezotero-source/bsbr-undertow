#pragma once
#include "PluginProcessor.h"

struct BsbrLook : juce::LookAndFeel_V4 {
  juce::Colour ink{0xfff2f0ea}, bg{0xff0a0a0a}, dim{0xff5e5e5a}, faint{0xff1c1c1b};
  juce::Font mono(float h, bool bold = false) const {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
  }
  BsbrLook() {
    setColour(juce::ResizableWindow::backgroundColourId, bg);
    setColour(juce::ComboBox::backgroundColourId, bg); setColour(juce::ComboBox::textColourId, ink); setColour(juce::ComboBox::outlineColourId, dim);
    setColour(juce::ComboBox::arrowColourId, ink);
    setColour(juce::PopupMenu::backgroundColourId, bg); setColour(juce::PopupMenu::textColourId, ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, ink); setColour(juce::PopupMenu::highlightedTextColourId, bg);
    setColour(juce::TextButton::buttonColourId, bg); setColour(juce::TextButton::buttonOnColourId, ink);
    setColour(juce::TextButton::textColourOffId, ink); setColour(juce::TextButton::textColourOnId, bg);
    setColour(juce::Slider::textBoxTextColourId, ink); setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack); setColour(juce::Slider::textBoxHighlightColourId, dim);
    setColour(juce::Label::textColourId, ink); setColour(juce::TextEditor::textColourId, ink); setColour(juce::TextEditor::backgroundColourId, bg);
    setColour(juce::CaretComponent::caretColourId, ink);
  }
  juce::Font getComboBoxFont(juce::ComboBox&) override { return mono(12); }
  juce::Font getPopupMenuFont() override { return mono(12); }
  juce::Font getTextButtonFont(juce::TextButton&, int) override { return mono(10); }
  juce::Font getLabelFont(juce::Label&) override { return mono(10); }
  juce::Label* createSliderTextBox(juce::Slider& s) override {
    auto* l = LookAndFeel_V4::createSliderTextBox(s); l->setFont(mono(10)); l->setColour(juce::Label::textColourId, ink); return l;
  }
  void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool on = b.getToggleState() || down;
    g.setColour(on ? ink : (over ? faint : bg)); g.fillRect(r);
    g.setColour(on ? ink : dim); g.drawRect(r, 1.f);
  }
  void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override {
    auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4);
    float d = std::min(r.getWidth(), r.getHeight()), cx = r.getCentreX(), cy = r.getCentreY(), rad = d / 2;
    juce::Path track; track.addCentredArc(cx, cy, rad, rad, 0, a0, a1, true);
    g.setColour(dim); g.strokePath(track, juce::PathStrokeType(1.f));
    float a = a0 + pos * (a1 - a0);
    juce::Path arc; arc.addCentredArc(cx, cy, rad, rad, 0, a0, a, true);
    g.setColour(ink); g.strokePath(arc, juce::PathStrokeType(2.2f));
    g.drawLine(cx + std::sin(a) * rad * 0.3f, cy - std::cos(a) * rad * 0.3f, cx + std::sin(a) * rad, cy - std::cos(a) * rad, 2.f);
  }
  void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override {
    auto r = b.getLocalBounds().toFloat().reduced(1);
    bool on = b.getToggleState();
    g.setColour(on ? ink : (over ? faint : bg)); g.fillRect(r);
    g.setColour(on ? ink : dim); g.drawRect(r, 1.f);
    g.setColour(on ? bg : ink); g.setFont(mono(10));
    g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
  }
};

// a row of buttons bound to a choice parameter (Live-style tab)
class TabRow : public juce::Component {
public:
  TabRow(juce::RangedAudioParameter& p, juce::StringArray names) : param(p),
    att(p, [this](float v) { setSel(juce::roundToInt(v)); }, nullptr) {
    for (int i = 0; i < names.size(); i++) {
      auto* b = btns.add(new juce::TextButton(names[i]));
      b->setClickingTogglesState(false);
      b->onClick = [this, i] { att.setValueAsCompleteGesture((float)i); };
      addAndMakeVisible(b);
    }
    att.sendInitialUpdate();
  }
  void resized() override {
    float w = (float)getWidth() / std::max(1, btns.size());
    for (int i = 0; i < btns.size(); i++) btns[i]->setBounds(juce::roundToInt(i * w), 0, juce::roundToInt((i + 1) * w) - juce::roundToInt(i * w) + (i < btns.size() - 1 ? 1 : 0), getHeight());
  }
private:
  void setSel(int s) { for (int i = 0; i < btns.size(); i++) btns[i]->setToggleState(i == s, juce::dontSendNotification); }
  juce::RangedAudioParameter& param;
  juce::OwnedArray<juce::TextButton> btns;
  juce::ParameterAttachment att;
};

class UndertowEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit UndertowEditor(UndertowProcessor&);
  ~UndertowEditor() override;
  void paint(juce::Graphics&) override;
  void resized() override;
private:
  void timerCallback() override;
  UndertowProcessor& proc;
  BsbrLook look;
  struct Ctl {
    int param = 0, kind = 0, row = 0, width = 0;
    std::unique_ptr<juce::Slider> s; std::unique_ptr<juce::SliderParameterAttachment> sa;
    std::unique_ptr<juce::ToggleButton> t; std::unique_ptr<juce::ButtonParameterAttachment> ta;
    std::unique_ptr<TabRow> tab;
    juce::Rectangle<int> labelBox; juce::String label;
  };
  struct Section { juce::String name; std::vector<int> ctls; juce::Rectangle<int> box; };
  std::vector<std::unique_ptr<Ctl>> ctls;
  std::vector<Section> sections;
  juce::ComboBox presets;
  juce::TextButton prev{"<"}, next{">"}, hit{"hit"};
  juce::Label desc;
  // impact animation
  struct Shock { double t0; float v; };
  std::vector<Shock> shocks;
  int lastHits = 0; float meterV = 0;
  juce::Rectangle<int> strip, meterBox;
  double now() const { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
  void syncPreset();
};
