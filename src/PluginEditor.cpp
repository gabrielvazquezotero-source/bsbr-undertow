#include "PluginEditor.h"
using namespace undertow;

static constexpr int KW = 60, KH = 74, ROWH = 92, HEAD = 96, PAD = 16, TOGW = 52;

UndertowEditor::UndertowEditor(UndertowProcessor& p) : juce::AudioProcessorEditor(&p), proc(p) {
  setLookAndFeel(&look);
  for (int i = 0; i < kNumParams; i++) {
    const ParamDef& d = kParams[i];
    if (sections.empty() || sections.back().name != d.section) sections.push_back({d.section, {}, {}});
    auto c = std::make_unique<Ctl>();
    c->param = i; c->kind = d.kind; c->row = d.row; c->label = d.label;
    auto& prm = *proc.par(i);
    if (d.kind == kDial) {
      c->s = std::make_unique<juce::Slider>(juce::Slider::RotaryVerticalDrag, juce::Slider::TextBoxBelow);
      c->s->setRotaryParameters(juce::degreesToRadians(225.f), juce::degreesToRadians(495.f), true);
      c->s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, KW + 6, 14);
      c->s->setDoubleClickReturnValue(true, prm.convertFrom0to1(prm.getDefaultValue()));
      c->s->setVelocityBasedMode(false); c->s->setMouseDragSensitivity(220);
      c->s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
      c->s->setColour(juce::Slider::textBoxTextColourId, look.ink);
      c->sa = std::make_unique<juce::SliderParameterAttachment>(prm, *c->s, nullptr);
      addAndMakeVisible(*c->s); c->width = KW;
    } else if (d.kind == kToggle) {
      c->t = std::make_unique<juce::ToggleButton>(d.label);
      c->ta = std::make_unique<juce::ButtonParameterAttachment>(prm, *c->t, nullptr);
      addAndMakeVisible(*c->t); c->width = TOGW;
    } else {
      auto names = juce::StringArray::fromTokens(d.choices, "|", "");
      c->tab = std::make_unique<TabRow>(prm, names);
      addAndMakeVisible(*c->tab);
      int per = 0; for (auto& n : names) per = std::max(per, n.length());
      c->width = names.size() * std::max(34, per * 7 + 12);
    }
    sections.back().ctls.push_back((int)ctls.size());
    ctls.push_back(std::move(c));
  }
  for (int i = 0; i < kNumPresets; i++) presets.addItem(juce::String::fromUTF8(kPresets[i].name), i + 1);
  presets.onChange = [this] { int id = presets.getSelectedId(); if (id > 0 && id - 1 != proc.cur.load()) proc.loadPreset(id - 1); syncPreset(); };
  prev.onClick = [this] { proc.loadPreset((proc.cur.load() + kNumPresets - 1) % kNumPresets); syncPreset(); };
  next.onClick = [this] { proc.loadPreset((proc.cur.load() + 1) % kNumPresets); syncPreset(); };
  hit.onClick = [this] { proc.audition(1.f); };
  desc.setFont(look.mono(10)); desc.setColour(juce::Label::textColourId, look.dim);
  for (auto* c : std::initializer_list<juce::Component*>{&presets, &prev, &next, &hit, &desc}) addAndMakeVisible(c);
  syncPreset();
  lastHits = proc.hits.load();

  // lay the sections out left to right, wrapping into a second line
  const int maxW = 1330;
  int x = PAD, y = HEAD, lineH = 0, W = 0;
  for (auto& s : sections) {
    int rw[2] = {0, 0}, rows = 1;
    for (int ci : s.ctls) { auto& c = *ctls[ci]; rw[c.row] += c.width + 6; rows = std::max(rows, c.row + 1); }
    int w = std::max(rw[0], rw[1]) + 14, h = 22 + rows * ROWH + 4;
    if (x + w > maxW) { x = PAD; y += lineH + 12; lineH = 0; }
    s.box = {x, y, w, h};
    x += w + 10; lineH = std::max(lineH, h); W = std::max(W, x);
  }
  setSize(std::max(W + PAD - 10, 900), y + lineH + PAD);
  startTimerHz(60);
}

UndertowEditor::~UndertowEditor() { setLookAndFeel(nullptr); }

void UndertowEditor::syncPreset() {
  int c = proc.cur.load();
  presets.setSelectedId(c + 1, juce::dontSendNotification);
  desc.setText(juce::String::fromUTF8(kPresets[c].desc), juce::dontSendNotification);
}

void UndertowEditor::resized() {
  int w = getWidth();
  hit.setBounds(PAD + 190, 18, 44, 22);
  prev.setBounds(w - PAD - 290, 18, 24, 22);
  presets.setBounds(w - PAD - 262, 18, 200, 22);
  next.setBounds(w - PAD - 58, 18, 24, 22);
  desc.setBounds(w - PAD - 290, 42, 290, 16);
  strip = {PAD, 66, w - 2 * PAD - 24, 18};
  meterBox = {w - PAD - 16, 18, 8, 66};
  for (auto& s : sections) {
    int rx[2] = {s.box.getX() + 8, s.box.getX() + 8};
    for (int ci : s.ctls) {
      auto& c = *ctls[ci];
      int ry = s.box.getY() + 22 + c.row * ROWH;
      int& x = rx[c.row];
      c.labelBox = {x - 3, ry, c.width + 6, 14};
      if (c.s) c.s->setBounds(x - 3, ry + 14, c.width + 6, KH);
      else if (c.t) c.t->setBounds(x, ry + 34, c.width, 22);
      else if (c.tab) c.tab->setBounds(x, ry + 34, c.width, 22);
      x += c.width + 6;
    }
  }
}

void UndertowEditor::timerCallback() {
  int h = proc.hits.load();
  if (h != lastHits) { lastHits = h; shocks.push_back({now(), juce::jlimit(0.2f, 1.f, proc.hitVel.load())}); }
  float m = proc.meter.load(); meterV = std::max(m, meterV * 0.9f);
  if (proc.cur.load() + 1 != presets.getSelectedId()) syncPreset();
  double t = now();
  shocks.erase(std::remove_if(shocks.begin(), shocks.end(), [t](const Shock& s) { return t - s.t0 > 1.2; }), shocks.end());
  repaint(0, 0, getWidth(), HEAD);
}

void UndertowEditor::paint(juce::Graphics& g) {
  g.fillAll(look.bg);
  double t = now();
  // the impact: on every hit the brand jolts and a shock front runs across the strip
  float jolt = 0; for (auto& s : shocks) { double a = t - s.t0; jolt += s.v * (float)(std::exp(-a * 18) * std::sin(a * 90)); }
  g.setColour(look.ink); g.setFont(look.mono(20, true));
  g.drawText("bsbr", PAD + juce::roundToInt(jolt * 3), 14, 60, 28, juce::Justification::left);
  g.setFont(look.mono(20)); g.drawText("undertow", PAD + 62 - juce::roundToInt(jolt * 2), 14, 120, 28, juce::Justification::left);
  g.setColour(look.dim); g.setFont(look.mono(10));
  g.drawText("kick + rumble  /  MIDI in fires the kick, Pitch sets the note", PAD, 42, 520, 16, juce::Justification::left);

  // strip: a still field of thin bars; shocks light it up as they pass
  {
    auto r = strip; int n = r.getWidth() / 3;
    for (int i = 0; i < n; i++) {
      float u = (float)i / n, lit = 0;
      for (auto& s : shocks) {
        double a = t - s.t0; float front = (float)(a / 0.45);
        float d = u - front; float env = (float)std::exp(-a * 3.2);
        if (d < 0) lit += s.v * env * std::exp(d * 7.f); // behind the front: a decaying wake
        lit += s.v * (float)std::exp(-a * 14) * 0.6f;      // flash
      }
      lit = juce::jlimit(0.f, 1.f, lit);
      float base = 0.25f + 0.2f * std::sin(u * 40.f) * std::sin(u * 7.f);
      float hgt = (base + lit * 0.75f) * r.getHeight();
      g.setColour(look.dim.interpolatedWith(look.ink, lit));
      g.fillRect((float)r.getX() + i * 3, r.getCentreY() - hgt / 2, 1.5f, hgt);
    }
  }
  // output meter
  g.setColour(look.faint); g.fillRect(meterBox);
  float mh = juce::jlimit(0.f, 1.f, (20.f * std::log10(meterV + 1e-6f) + 48.f) / 48.f) * meterBox.getHeight();
  g.setColour(meterV > 0.97f ? juce::Colour(0xffe05a4a) : look.ink);
  g.fillRect((float)meterBox.getX(), (float)meterBox.getBottom() - mh, (float)meterBox.getWidth(), mh);

  // sections
  for (auto& s : sections) {
    g.setColour(look.dim); g.drawRect(s.box, 1);
    g.setColour(look.ink); g.setFont(look.mono(10, true));
    g.drawText(s.name, s.box.getX() + 8, s.box.getY() + 4, s.box.getWidth() - 12, 14, juce::Justification::left);
    g.setFont(look.mono(10));
    for (int ci : s.ctls) {
      auto& c = *ctls[ci];
      if (c.t) continue;
      g.setColour(look.ink.withAlpha(0.85f));
      g.drawText(c.label, c.labelBox.withY(c.labelBox.getY() + (c.tab ? 16 : 0)), c.tab ? juce::Justification::left : juce::Justification::centred);
    }
  }
}
