#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;
using namespace undertow;

namespace ui {
static const int B4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}; // 4x4 Bayer: the bsbr dither

Font mono(float height, float kerning) {
  static Typeface::Ptr face = Typeface::createSystemTypefaceFor(BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
  return Font(FontOptions(face).withHeight(height)).withExtraKerningFactor(kerning);
}

static float textW(const Font& f, const String& s) { return GlyphArrangement::getStringWidth(f, s); }

static void text(Graphics& g, const String& s0, float x, float y, float h, float kern, Colour c, Justification j = Justification::left, float w = 0) {
  const String s = s0.replace(" . ", String(CharPointer_UTF8(" \xc2\xb7 "))).replace("+/-", String(CharPointer_UTF8("\xc2\xb1")));
  g.setColour(c);
  g.setFont(mono(h, kern));
  if (w <= 0) w = textW(mono(h, kern), s) + 4;
  float left = x;
  if (j.testFlags(Justification::right)) left = x - w;
  else if (j.testFlags(Justification::horizontallyCentred)) left = x - w / 2;
  g.drawText(s, Rectangle<float>(left, y - h, w, h * 1.3f), j.getOnlyHorizontalFlags() | Justification::top, false);
}

static String dbText(double v) {
  const double r = std::round(v * 10) / 10;
  return (r > 0 ? "+" : r < 0 ? "-" : "") + String(std::abs(r), 1) + " dB";
}

// upper case that also handles the accents in the Galician names (towupper depends on the locale)
static String up(const String& s) {
  String r = s.toUpperCase();
  const char* lo[] = {"\xc3\xa1", "\xc3\xa9", "\xc3\xad", "\xc3\xb3", "\xc3\xba", "\xc3\xb1", "\xc3\xbc"};
  const char* hi[] = {"\xc3\x81", "\xc3\x89", "\xc3\x8d", "\xc3\x93", "\xc3\x9a", "\xc3\x91", "\xc3\x9c"};
  for (int i = 0; i < 7; i++) r = r.replace(String(CharPointer_UTF8(lo[i])), String(CharPointer_UTF8(hi[i])));
  return r;
}

static PixelARGB px(Colour c) { return PixelARGB(255, c.getRed(), c.getGreen(), c.getBlue()); }

//==============================================================================
void LnF::drawButtonBackground(Graphics& g, Button& b, const Colour&, bool over, bool down) {
  auto r = b.getLocalBounds().toFloat().reduced(0.75f);
  const float rad = r.getHeight() / 2;
  if (b.getToggleState() || down) { g.setColour(t.fg); g.fillRoundedRectangle(r, rad); }
  else if (over) { g.setColour(t.dim.withAlpha(0.45f)); g.fillRoundedRectangle(r, rad); }
  g.setColour(t.fg);
  g.drawRoundedRectangle(r, rad, 1.5f);
}

void LnF::drawButtonText(Graphics& g, TextButton& b, bool, bool down) {
  const float fs = (float)b.getProperties().getWithDefault("fs", 11.0);
  g.setFont(mono(fs, 0.12f));
  g.setColour(b.getToggleState() || down ? t.bg : t.fg);
  g.drawText(b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

void LnF::drawLinearSlider(Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle, Slider& s) {
  const float cy = (float)y + (float)h / 2;
  g.setColour(t.mut);
  for (float xx = (float)x; xx <= (float)(x + w); xx += 3.5f) g.fillEllipse(xx - 0.75f, cy - 0.75f, 1.5f, 1.5f);
  const bool bip = s.getProperties().getWithDefault("bipolar", false);
  const float c = (float)x + (float)w / 2, from = bip ? c : (float)x;
  if (bip) g.fillRect(c - 0.75f, cy - 6, 1.5f, 12.0f);
  g.setColour(t.fg);
  g.fillRect(jmin(from, pos), cy - 1.5f, std::abs(pos - from), 3.0f);
  g.setColour(t.bg); g.fillEllipse(pos - 6, cy - 6, 12, 12);
  g.setColour(t.fg); g.drawEllipse(pos - 4.75f, cy - 4.75f, 9.5f, 9.5f, 2.5f);
}

PopupMenu::Options LnF::getOptionsForComboBoxPopupMenu(ComboBox& b, Label& l) { return LookAndFeel_V4::getOptionsForComboBoxPopupMenu(b, l); }
void LnF::drawPopupMenuBackground(Graphics& g, int w, int h) { g.fillAll(t.bg); g.setColour(t.fg); g.drawRect(0, 0, w, h, 1); }
void LnF::drawPopupMenuSectionHeader(Graphics& g, const Rectangle<int>& r, const String& s) {
  text(g, s.toUpperCase(), (float)r.getX() + 12, (float)r.getBottom() - 6, 9, 0.24f, t.mut);
}
void LnF::getIdealPopupMenuItemSize(const String& s, bool sep, int, int& w, int& h) {
  w = (int)textW(mono(11, 0.08f), s) + 60; h = sep ? 9 : 22;
}
void LnF::drawPopupMenuItem(Graphics& g, const Rectangle<int>& r, bool sep, bool, bool hi, bool ticked, bool, const String& s, const String&, const Drawable*, const Colour*) {
  if (sep) { g.setColour(t.dim); g.fillRect(r.getX() + 10, r.getCentreY(), r.getWidth() - 20, 1); return; }
  if (hi) { g.setColour(t.fg); g.fillRect(r.reduced(3, 1)); }
  const Colour c = hi ? t.bg : t.fg;
  // "name|description": the description dimmed after the name
  const String name = s.upToFirstOccurrenceOf("|", false, false), desc = s.fromFirstOccurrenceOf("|", false, false);
  text(g, name, (float)r.getX() + 14, (float)r.getCentreY() + 5, 11, 0.08f, c);
  if (desc.isNotEmpty()) text(g, desc, (float)r.getX() + 170, (float)r.getCentreY() + 4, 9, 0.06f, hi ? t.bg : t.mut);
  if (ticked) { g.setColour(c); g.fillRect((float)r.getX() + 5, (float)r.getCentreY() - 2.5f, 5.0f, 5.0f); }
}

//==============================================================================
RopeRow::RopeRow(RangedAudioParameter& p, const String& l, bool bip, const Theme& t, int lw)
    : theme(t), param(p), label(l), labelW(lw), att(p, slider, nullptr) {
  slider.setSliderStyle(Slider::LinearHorizontal);
  slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
  slider.getProperties().set("bipolar", bip);
  slider.setDoubleClickReturnValue(true, param.convertFrom0to1(param.getDefaultValue()));
  slider.setVelocityBasedMode(false);
  slider.setMouseDragSensitivity(400);
  slider.onValueChange = [this] { repaint(); };
  addAndMakeVisible(slider);
}

void RopeRow::paint(Graphics& g) {
  const float h = (float)getHeight();
  text(g, label, 0, h / 2 + 4, 9, 0.12f, theme.mut);
  text(g, param.getCurrentValueAsText(), (float)getWidth(), h / 2 + 4.5f, 10, 0.0f, theme.fg, Justification::right, 80);
}

void RopeRow::resized() { slider.setBounds(labelW, 0, getWidth() - labelW - 66, getHeight()); }

//==============================================================================
ChoicePills::ChoicePills(RangedAudioParameter& p, const StringArray& labels, float fontSize)
    : fs(fontSize), att(p, [this](float v) {
        const int idx = roundToInt(v);
        for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState(i == idx, dontSendNotification);
      }) {
  for (int i = 0; i < labels.size(); ++i) {
    auto* b = buttons.add(new TextButton(labels[i]));
    b->getProperties().set("fs", fs);
    b->onClick = [this, i] { att.setValueAsCompleteGesture((float)i); };
    addAndMakeVisible(b);
  }
  att.sendInitialUpdate();
}

int ChoicePills::preferredWidth() const {
  float w = 0;
  for (auto* b : buttons) w += textW(mono(fs, 0.12f), b->getButtonText()) + fs * 2.0f + 4;
  return (int)w;
}

void ChoicePills::resized() {
  float x = 0;
  for (auto* b : buttons) {
    const float w = textW(mono(fs, 0.12f), b->getButtonText()) + fs * 2.0f;
    b->setBounds((int)x, 0, (int)w, getHeight());
    x += w + 4;
  }
}

//==============================================================================
Tide::Tide(UndertowProcessor& p, const Theme& t) : proc(p), theme(t) {
  cols.assign(W, {}); tmp.resize(Engine::RING);
  proc.setColumnsAcross(W);
  lastHits = proc.hits.load();
  setInterceptsMouseClicks(false, false);
}

void Tide::tick() {
  const int n = proc.pullColumns(tmp.data(), (int)tmp.size());
  for (int i = 0; i < n; i++) { cols[(size_t)head] = tmp[(size_t)i]; head = (head + 1) % W; }
  const double now = Time::getMillisecondCounterHiRes() / 1000.0;
  const int h = proc.hits.load();
  if (h != lastHits) { lastHits = h; shocks.push_back({now, jlimit(0.25f, 1.0f, proc.hitVel.load())}); }
  shocks.erase(std::remove_if(shocks.begin(), shocks.end(), [now](const Shock& s) { return now - s.t0 > 1.6; }), shocks.end());

  const float wl = H * 0.44f, up = wl - 14, down = H - wl - 22;
  const PixelARGB fg = px(theme.fg), bg = px(theme.bg);
  // per column: kick height above the waterline, current depth below
  float hd[W], hw[W];
  for (int x = 0; x < W; x++) {
    const auto& c = cols[(size_t)((head + x) % W)];
    hd[x] = jlimit(0.0f, 1.0f, (20 * std::log10(c.dry + 1e-6f) + 42) / 42) * up;
    hw[x] = jlimit(0.0f, 1.0f, (20 * std::log10(c.wet + 1e-6f) + 54) / 54) * down;
  }
  outNow = cols[(size_t)((head + W - 1) % W)].out;
  const float flow = (float)std::fmod(now * 5.0, 6283.0);
  Image::BitmapData bd(pix, Image::BitmapData::writeOnly);
  for (int y = 0; y < H; y++) {
    auto* row = bd.getLinePointer(y);
    for (int x = 0; x < W; x++) {
      float f = 0;
      const float yy = (float)y + 0.5f;
      if (yy < wl) {
        const float d = wl - yy;
        if (d < hd[x]) f = 0.42f + 0.5f * (d / jmax(1.0f, hd[x]));          // the hit, brightest at its crest
      } else {
        const float d = yy - wl;
        if (d < hw[x]) {                                                     // the current: flows back out, under the kick
          const float k = 1 - d / jmax(1.0f, hw[x]);
          f = (0.18f + 0.5f * k) * (0.6f + 0.4f * std::sin((float)x * 0.16f + d * 0.42f + flow));
        }
      }
      // shock: a ring from the newest hit, at the waterline on the right
      for (auto& s : shocks) {
        const float a = (float)(now - s.t0), r = a * 560.0f, dist = std::hypot((float)(W - 1 - x), (yy - wl) * 1.6f), e = std::abs(dist - r);
        if (e < 7) f = jmax(f, s.v * std::exp(-a * 2.4f) * (1 - e / 7) * 0.95f);
      }
      const bool on = f * 16.0f > (float)B4[(y & 3) * 4 + (x & 3)];
      *reinterpret_cast<PixelARGB*>(row + x * bd.pixelStride) = on ? fg : bg;
    }
  }
  repaint();
}

void Tide::paint(Graphics& g) {
  g.setImageResamplingQuality(Graphics::lowResamplingQuality);
  g.drawImageAt(pix, 0, 0);
  const float wl = H * 0.44f;
  g.setColour(theme.fg); g.drawLine(0, wl, (float)W, wl, 1.5f);
  // time marks
  for (int s = 0; s <= 4; s++) {
    const float x = (float)W - 1 - (float)s * (W - 1) / 4.0f;
    g.setColour(theme.mut); g.drawLine(x, (float)H - 16, x, (float)H - 11, 1.0f);
    g.setColour(theme.bg); g.fillRect(s == 0 ? x - 24 : s == 4 ? x : x - 16, (float)H - 11, s == 0 ? 24.0f : 32.0f, 11.0f);
    text(g, s == 0 ? String("now") : "-" + String(s) + " s", x, (float)H - 1, 9, 0.06f, theme.mut, s == 0 ? Justification::right : s == 4 ? Justification::left : Justification::horizontallyCentred, 40);
  }
  auto tag = [&](const String& s, float x, float y, Colour c, bool right) {
    const float w = textW(mono(9, 0.14f), s.replace(" . ", " - ")) + 10;
    g.setColour(theme.bg); g.fillRect(right ? x - w : x - 3, y - 10, w, 14.0f);
    text(g, s, right ? x - 4 : x + 2, y + 1, 9, 0.14f, c, right ? Justification::right : Justification::left, w);
  };
  tag("SURFACE . THE KICK", 0, 11, theme.mut, false);
  tag("BELOW . HAZE + ECHO", 0, wl + 15, theme.mut, false);
  tag("OUT " + (outNow < 1e-4f ? String("-") : dbText(20 * std::log10(outNow))), (float)W, 11, theme.fg, true);
}

//==============================================================================
Shape::Shape(UndertowProcessor& p, const Theme& t) : proc(p), theme(t) { setInterceptsMouseClicks(false, false); }

void Shape::compute() {
  // same maths as the engine's synth (noise seeded the same way, so Body draws as it sounds)
  const double R = 8000, L = jmax(0.05, proc.getP("klen") / 1000), sw = jmax(0.005, proc.getP("ksweep") / 1000);
  const double f0 = 440 * std::pow(2.0, (std::round(proc.getP("kpitch")) - 69) / 12), pu = proc.getP("kpunch") - 1, cv = 1 + proc.getP("kcurve") / 100 * 7;
  const double sh = proc.getP("kshape") / 100, body = proc.getP("kbody") / 100, dr = proc.getP("kdrop") / 12, at = jmax(0.0003, proc.getP("katt") / 1000);
  const int n = (int)(L * R);
  const double c = 1 - std::exp(-TAU * (350 + 2600 * sh) / R), nc = 1 - std::exp(-TAU * 160 / R), th = std::tanh(1.5);
  double ph = 0, l1 = 0, l2 = 0, n1 = 0, n2 = 0; uint32_t seed = 0x9E3779B9u;
  env.assign(W, 0.0f);
  for (int i = 0; i < n; i++) {
    const double t = i / R, e = t < sw ? std::pow(1 - t / sw, cv) : 0;
    ph += f0 * (1 + pu * e) * std::pow(2.0, -dr * t / L) / R; if (ph > 1) ph -= 1;
    double s = std::sin(ph * TAU);
    if (sh > 0.001) { const double tri = 2 / PI * std::asin(s); s = s * (1 - sh) + std::sin(tri * 1.4) / std::sin(1.4) * sh; }
    const double att = t < at ? std::pow(std::sin(PI / 2 * t / at), 2) : 1, amp = std::exp(-6.9 * std::pow(t / L, 1.15)) * att;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; const double rn = (seed / 4294967296.0) * 2 - 1;
    n1 += (rn - n1) * nc; n2 += (n1 - n2) * nc;
    double x = std::tanh((s * amp + n2 * 9 * body * std::exp(-t / 0.07)) * 1.5) / th;
    l1 += (x - l1) * c; l2 += (l1 - l2) * c;
    const int px_ = jmin(W - 1, (int)((double)i / n * W));
    env[(size_t)px_] = jmax(env[(size_t)px_], (float)std::abs(l2));
  }
  len = (float)L; sweepAt = (float)jmin(1.0, sw / L);
}

void Shape::tick() {
  String k;
  for (auto* id : {"klen", "ksweep", "kpitch", "kpunch", "kcurve", "kshape", "kbody", "kdrop", "katt"}) k << String(proc.getP(id), 3) << ";";
  if (k != key) { key = k; compute(); }
  pos = proc.voicePos.load();
  const PixelARGB fg = px(theme.fg), bg = px(theme.bg);
  const float cy = H / 2.0f;
  Image::BitmapData bd(pix, Image::BitmapData::writeOnly);
  for (int y = 0; y < H; y++) {
    auto* row = bd.getLinePointer(y);
    for (int x = 0; x < W; x++) {
      const float a = env[(size_t)x] * (cy - 2), d = std::abs((float)y + 0.5f - cy);
      float f = d < a ? (a - d < 1.6f ? 1.0f : 0.38f) : 0.0f;
      if (pos >= 0 && (float)x < pos * W && d < a) f = jmax(f, 0.75f);   // the part already played
      const bool on = f * 16.0f > (float)B4[(y & 3) * 4 + (x & 3)];
      *reinterpret_cast<PixelARGB*>(row + x * bd.pixelStride) = on ? fg : bg;
    }
  }
  repaint();
}

void Shape::paint(Graphics& g) {
  g.drawImageAt(pix, 0, 0);
  g.setColour(theme.dim); g.drawLine(0, H / 2.0f, (float)W, H / 2.0f, 1.0f);
  const float sx = sweepAt * W;
  Path l; l.startNewSubPath(sx, 0); l.lineTo(sx, (float)H);
  Path d; const float dash[] = {1.5f, 3.0f}; PathStrokeType(1.0f).createDashedStroke(d, l, dash, 2);
  g.setColour(theme.mut); g.fillPath(d);
  text(g, "SWEEP", sx + 4, 10, 8, 0.12f, theme.mut);
  text(g, (len < 1 ? String(roundToInt(len * 1000)) + " ms" : String(len, 2) + " s"), (float)W, 10, 9, 0.06f, theme.mut, Justification::right, 60);
  if (pos >= 0 && pos <= 1) { g.setColour(theme.fg); g.fillRect(pos * W - 0.75f, 0.0f, 1.5f, (float)H); }
}

//==============================================================================
void Meters::paint(Graphics& g) {
  const float rowH = 20, lw = 80, vw = 70, cw = (float)getWidth() - lw - vw;
  const int N = seal ? 13 : 21;
  auto cells = [&](float y, std::function<Colour(int)> col) {
    const float gap = 2, d = (cw - gap * (float)(N - 1)) / (float)N;
    for (int i = 0; i < N; ++i) { g.setColour(col(i)); g.fillEllipse(lw + (float)i * (d + gap) + (d - 7) / 2, y - 3.5f, 7, 7); }
  };
  auto bar = [&](float y, float frac, int mark) {
    const int lit = roundToInt(jlimit(0.0f, 1.0f, frac) * N);
    cells(y, [&](int i) { return i < lit ? theme.fg : i == mark ? theme.mut : theme.dim; });
  };
  auto bip = [&](float y, float v) {
    const float c = (N - 1) / 2.0f, t = c + jlimit(-1.0f, 1.0f, v) * c;
    cells(y, [&](int i) { const bool on = (float)i >= jmin(c, t) - 0.5f && (float)i <= jmax(c, t) + 0.5f && std::abs(v) > 0.02f; return on ? theme.fg : std::abs((float)i - c) < 0.6f ? theme.mut : theme.dim; });
  };
  auto row = [&](int i, const String& l, const String& v) {
    const float y = (float)i * rowH + rowH / 2;
    text(g, l, 0, y + 4, 9, 0.12f, theme.mut);
    text(g, v, (float)getWidth(), y + 4.5f, 10, 0, theme.fg, Justification::right, vw);
    return y;
  };
  const String dash = "-";
  if (seal) {
    const char* names[3] = {"LOW", "MID", "HIGH"};
    for (int j = 0; j < 3; j++) bar(row(j, names[j], r.seal[j] < 0.05f ? dash : dbText(-r.seal[j])), r.seal[j] / 12, -1);
    return;
  }
  bar(row(0, "DRIVE", r.drv < 0.01f ? String("clean") : String(roundToInt(jmin(r.drv, 9.99f) * 100)) + " %"), r.drv / 1.5f, -1);
  auto duck = [&](float d) { return d < 0.01f ? dash : dbText(20 * std::log10(jmax(1e-3f, 1 - d))); };
  bar(row(1, "DUCK HAZE", duck(r.dv)), r.dv, -1);
  bar(row(2, "DUCK ECHO", duck(r.de)), r.de, -1);
  bip(row(3, "OTT", r.od < 0.1f && r.ou < 0.1f ? dash : "-" + String(r.od, 1) + " / +" + String(r.ou, 1)), (r.ou - r.od) / 12);
  bar(row(4, "CLIP", r.clip < 0.005f ? dash : dbText(20 * std::log10(jmax(1e-3f, 1 - r.clip)))), r.clip * 4, -1);
  const float od = 20 * std::log10(r.out + 1e-9f);
  bar(row(5, "PEAK OUT", od < -90 ? dash : dbText(od)), (od + 30) / 30.5f, N - 1);
}

//==============================================================================
static const int LX = 24, LW = 596, RX = 648, RW = 528, MY = 508, MW = 270;
static const int MX[4] = {24, 318, 612, 906};

void Content::addRope(const char* id, const char* label, bool bip, int labelW) {
  ropeIdx[id] = ropes.size();
  addAndMakeVisible(ropes.add(new RopeRow(*proc.apvts.getParameter(id), label, bip, lnf.t, labelW)));
}
void Content::addPills(const char* id, StringArray labels, float fs) {
  pillIdx[id] = pills.size();
  addAndMakeVisible(pills.add(new ChoicePills(*proc.apvts.getParameter(id), labels, fs)));
}
RopeRow& Content::rope(const char* id) { return *ropes[ropeIdx[id]]; }
ChoicePills& Content::pill(const char* id) { return *pills[pillIdx[id]]; }
void Content::place(const char* id, int x, int y, int w) { rope(id).setBounds(x, y, w, 19); }
void Content::placePills(const char* id, int x, int y, int h) { auto& p = pill(id); p.setBounds(x, y, p.preferredWidth(), h); }

Content::Content(UndertowProcessor& p, LnF& l)
    : proc(p), lnf(l), theme(l.t), tide(p, l.t), shape(p, l.t), reading(l.t, false), sealMeters(l.t, true) {
  logo = ImageCache::getFromMemory(BinaryData::bisbarralogo_png, BinaryData::bisbarralogo_pngSize);
  // kick
  addRope("kpitch", "PITCH"); addRope("kpunch", "PUNCH"); addRope("ksweep", "SWEEP"); addRope("kcurve", "CURVE"); addRope("kshape", "SHAPE");
  addRope("klen", "LENGTH"); addRope("katt", "ATTACK"); addRope("kbody", "BODY"); addRope("kdrop", "DROP"); addRope("click", "CLICK");
  // beater, notch, drive
  addRope("ctone", "TONE", false, 80); addRope("nfreq", "NOTCH", false, 80); addRope("ndepth", "DEPTH", false, 80);
  addPills("npos", {"PRE DRIVE", "POST DRIVE"});
  addPills("dtype", {"BYPASS", "VALVE", "SINE", "ANALOG"}, 10);
  addRope("gain", "GAIN", false, 80); addRope("low", "LOW 80", true, 80); addRope("high", "HIGH 12K", true, 80);
  // haze
  addPills("mvmode", {"PHASER", "NOTCHES"});
  addRope("vsend", "FILL"); addRope("vsize", "SIZE"); addRope("vpre", "PRE"); addRope("vmod", "MOD"); addRope("vhpf", "HPF"); addRope("vdark", "LPF");
  addRope("mvamt", "MOVE"); addRope("mvrate", "RATE"); addRope("vduck", "DUCK");
  // echo
  addPills("ddiv", {"1/16", "1/8", "3/16", "1/4", "3/8"});
  addRope("dsend", "FILL"); addRope("dfb", "REPEATS"); addRope("dinto", "INTO HAZE"); addRope("ehpf", "HPF"); addRope("edark", "LPF"); addRope("educk", "DUCK");
  // ott, mix
  addRope("ott", "OTT"); addRope("otime", "TIME"); addRope("drel", "RETURN"); addRope("push", "PUSH"); addRope("dry", "DRY"); addRope("wet", "WET");
  addPills("cmode", {"SOFT CLIP", "LIMIT"});
  // seal
  addRope("squash", "SQUASH"); addRope("crush", "CRUSH"); addRope("smix", "MIX"); addRope("vol", "VOL");
  sealOn.setClickingTogglesState(true); sealOn.getProperties().set("fs", 9.0);
  sealAtt = std::make_unique<ButtonParameterAttachment>(*proc.apvts.getParameter("sealon"), sealOn, nullptr);
  // header
  addPills("listen", {"ALL", "DRY", "WET"}, 11);
  for (auto* b : {&prev, &next, &weather, &hit, &squall, &invert, &start}) { b->getProperties().set("fs", b == &weather ? 11.0 : 10.0); addAndMakeVisible(b); }
  start.getProperties().set("fs", 9.0);
  prev.onClick = [this] { proc.loadPreset((proc.cur.load() + kNumPresets - 1) % kNumPresets); };
  next.onClick = [this] { proc.loadPreset((proc.cur.load() + 1) % kNumPresets); };
  weather.onClick = [this] { showWeather(); };
  start.onClick = [this] { showStart(); };
  hit.onClick = [this] { proc.audition(1.0f); };
  squall.onClick = [this] { proc.squall(); };
  invert.onClick = [this] { proc.apvts.state.setProperty("ink", !(bool)proc.apvts.state.getProperty("ink", true), nullptr); };
  for (Component* c : std::initializer_list<Component*>{&sealOn, &tide, &shape, &reading, &sealMeters}) addAndMakeVisible(c);
  lastHits = proc.hits.load();
  applyTheme();
  setSize(W, H);
  startTimerHz(40);
}

Content::~Content() { stopTimer(); }

void Content::applyTheme() {
  lnf.t = (bool)proc.apvts.state.getProperty("ink", true) ? Theme::ink() : Theme::paper();
  repaint();
}

void Content::showWeather() {
  PopupMenu m;
  m.addSectionHeader("weather . 16 presets");
  for (int i = 0; i < kNumPresets; i++)
    m.addItem(i + 1, String(i + 1).paddedLeft('0', 2) + "  " + up(String::fromUTF8(kPresets[i].name)) + "|" + String::fromUTF8(kPresets[i].desc), true, i == proc.cur.load());
  m.showMenuAsync(PopupMenu::Options().withTargetComponent(&weather).withMinimumWidth(460), [this](int r) { if (r > 0) proc.loadPreset(r - 1); });
}

void Content::showStart() {
  PopupMenu m;
  const char* fam[3] = {"909", "808", "deep"};
  for (auto* f : fam) {
    m.addSectionHeader(f);
    for (int k = 0; k < kNumKit; k++)
      if (String(kKit[k].family) == f) m.addItem(k + 1, String(kKit[k].name).toUpperCase(), true, proc.kit.load() == k);
  }
  m.showMenuAsync(PopupMenu::Options().withTargetComponent(&start).withMinimumWidth(180), [this](int r) { if (r > 0) proc.startFrom(r - 1); });
}

void Content::resized() {
  // header
  prev.setBounds(300, 18, 28, 28); weather.setBounds(332, 18, 220, 28); next.setBounds(556, 18, 28, 28);
  hit.setBounds(618, 18, 62, 28); squall.setBounds(686, 18, 86, 28);
  auto& li = pill("listen"); li.setBounds(848, 18, li.preferredWidth(), 28);
  invert.setBounds(W - 24 - 82, 18, 82, 28);
  // 01 undertow, 02 kick
  tide.setBounds(LX, 110, Tide::W, Tide::H);
  start.setBounds(LX + 96, 324, 92, 20);
  shape.setBounds(LX, 352, Shape::W, Shape::H);
  const char* kl[5] = {"kpitch", "kpunch", "ksweep", "kcurve", "kshape"};
  const char* kr[5] = {"klen", "katt", "kbody", "kdrop", "click"};
  const int cw = (LW - 28) / 2;
  for (int i = 0; i < 5; i++) { place(kl[i], LX, 400 + i * 20, cw); place(kr[i], LX + cw + 28, 400 + i * 20, cw); }
  // 03 beater . notch, 04 drive, readings
  place("ctone", RX, 106, RW); place("nfreq", RX, 126, RW); place("ndepth", RX, 146, RW);
  placePills("npos", RX + 80, 170);
  placePills("dtype", RX + 80, 234, 22);
  place("gain", RX, 262, RW); place("low", RX, 282, RW); place("high", RX, 302, RW);
  reading.setBounds(RX, 366, RW, 120);
  // 05 haze, 06 echo, 07 ott . mix, 08 seal
  placePills("mvmode", MX[0] + 56, MY + 32);
  const char* hz[9] = {"vsend", "vsize", "vpre", "vmod", "vhpf", "vdark", "mvamt", "mvrate", "vduck"};
  for (int i = 0; i < 9; i++) place(hz[i], MX[0], MY + 58 + i * 19, MW);
  placePills("ddiv", MX[1] + 56, MY + 32);
  const char* ec[6] = {"dsend", "dfb", "dinto", "ehpf", "edark", "educk"};
  for (int i = 0; i < 6; i++) place(ec[i], MX[1], MY + 58 + i * 19, MW);
  const char* om[6] = {"ott", "otime", "drel", "push", "dry", "wet"};
  for (int i = 0; i < 6; i++) place(om[i], MX[2], MY + 32 + i * 19, MW);
  placePills("cmode", MX[2] + 56, MY + 152);
  sealOn.setBounds(MX[3], MY + 32, 64, 20);
  const char* se[4] = {"squash", "crush", "smix", "vol"};
  for (int i = 0; i < 4; i++) place(se[i], MX[3], MY + 58 + i * 19, MW);
  sealMeters.setBounds(MX[3], MY + 140, MW, 60);
}

void Content::paint(Graphics& g) {
  const Theme& t = theme;
  g.fillAll(t.bg);
  // header
  text(g, "b s b r", 24, 24, 10, 0.4f, t.mut);
  text(g, "undertow", 24, 52, 30, 0.14f, t.fg);
  text(g, "WEATHER", 290, 36, 9, 0.3f, t.mut, Justification::right, 80);
  text(g, "LISTEN", 840, 36, 9, 0.3f, t.mut, Justification::right, 80);
  // hit lamp: lights on every hit
  {
    const double now = Time::getMillisecondCounterHiRes() / 1000.0, a = now - flashT;
    const float v = a < 0.25 ? (float)(1 - a / 0.25) : 0;
    g.setColour(t.fg); g.drawRect(598.0f, 27.0f, 10.0f, 10.0f, 1.5f);
    if (v > 0) { g.setColour(t.fg.withAlpha(v)); g.fillRect(600.5f, 29.5f, 5.0f, 5.0f); }
  }
  text(g, "LATENCY", (float)invert.getX() - 62, 36, 9, 0.12f, t.mut, Justification::right, 80);
  text(g, "10", (float)invert.getX() - 54, 37, 14, 0, t.fg);
  text(g, "ms", (float)invert.getX() - 34, 36, 9, 0, t.mut);
  g.setColour(t.fg); g.fillRect(0.0f, 64.0f, (float)W, 1.5f);

  auto mod = [&](int x, int y, int w, const String& id, const String& name, const String& sub) {
    g.setColour(t.fg); g.fillRect((float)x, (float)y, (float)w, 3.0f);
    text(g, id, (float)x, (float)y + 20, 9, 0.1f, t.mut);
    text(g, name, (float)x + 18, (float)y + 21, 14, 0.24f, t.fg);
    if (sub.isNotEmpty()) text(g, sub, (float)(x + w), (float)y + 20, 9, 0.08f, t.mut, Justification::right, 330);
  };
  // 01 undertow (the hero has no rule, like beam's)
  text(g, "01", LX, 98, 9, 0.1f, t.mut);
  text(g, "UNDERTOW", LX + 18, 99, 14, 0.24f, t.fg);
  text(g, "the kick on the surface . the rumble pulled under it . 4 s", (float)(LX + LW), 97, 9, 0.12f, t.mut, Justification::right, 420);
  // 02 kick, with the note it is tuned to
  mod(LX, 312, LW, "02", "KICK", "");
  {
    const int n = roundToInt(proc.getP("kpitch"));
    static const char* N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const String nn = String(N[((n % 12) + 12) % 12]) + String(n / 12 - 2);
    text(g, String(440.0 * std::pow(2.0, (n - 69) / 12.0), 1) + " Hz", (float)(LX + LW), 340, 10, 0.1f, t.mut, Justification::right, 80);
    text(g, nn, (float)(LX + LW) - 84, 345, 28, 0.04f, t.fg, Justification::right, 100);
  }
  text(g, "one hit per note", (float)(LX + 198), 339, 9, 0.12f, t.mut);
  mod(RX, 78, RW, "03", "BEATER . NOTCH", "a beater on the head . a hole at 200-450 Hz");
  text(g, "POSITION", (float)RX, 184, 9, 0.12f, t.mut);
  mod(RX, 204, RW, "04", "DRIVE", "0 to +12 dB into the curve . shelves before it");
  text(g, "TYPE", (float)RX, 249, 9, 0.12f, t.mut);
  mod(RX, 336, RW, "~", "READING", "live");
  mod(MX[0], MY, MW, "05", "HAZE", "bright . long");
  text(g, "DRIFT", (float)MX[0], (float)MY + 46, 9, 0.12f, t.mut);
  mod(MX[1], MY, MW, "06", "ECHO", "synced");
  text(g, "TIME", (float)MX[1], (float)MY + 46, 9, 0.12f, t.mut);
  mod(MX[2], MY, MW, "07", "OTT . MIX", "");
  text(g, "STAGE", (float)MX[2], (float)MY + 166, 9, 0.12f, t.mut);
  mod(MX[3], MY, MW, "08", "SEAL", "bsbr");
  text(g, "squash per band", (float)MX[3] + 76, (float)MY + 46, 9, 0.08f, t.mut);

  // footer
  g.setColour(t.fg); g.fillRect(0.0f, (float)H - 34, (float)W, 1.5f);
  const int c = proc.cur.load();
  const String name = proc.kit.load() >= 0 ? String("FROM ") + String(kKit[proc.kit.load()].name).toUpperCase() : String(c + 1).paddedLeft('0', 2) + " " + up(String::fromUTF8(kPresets[c].name));
  text(g, name, 24, (float)H - 13, 10, 0.06f, t.mut);
  text(g, String::fromUTF8(kPresets[c].desc) + " . MIDI fires the kick, Pitch sets the note", 24 + textW(mono(10, 0.06f), name) + 18, (float)H - 13, 10, 0.06f, t.fg);
  text(g, "bisbarra.com", (float)W - 50, (float)H - 13, 10, 0.14f, t.fg, Justification::right, 120);
  if (logo.isValid()) { g.setColour(t.fg); g.drawImage(logo, Rectangle<float>((float)W - 42, (float)H - 27, 18, 19), RectanglePlacement::centred, true); }
}

void Content::timerCallback() {
  tide.tick();
  shape.tick();
  auto take = [](std::atomic<float>& a) { return a.exchange(0.0f); };
  auto hold = [](float& h, float v, float k = 0.9f) { h = jmax(v, h * k); };
  hold(held.drv, take(proc.mDrv)); hold(held.dv, take(proc.mDuckV)); hold(held.de, take(proc.mDuckE));
  hold(held.od, take(proc.mOttD), 0.92f); hold(held.ou, take(proc.mOttU), 0.92f);
  hold(held.clip, take(proc.mClip)); hold(held.out, take(proc.meter), 0.93f);
  for (int j = 0; j < 3; j++) hold(held.seal[j], take(proc.mSeal[j]));
  reading.r = held; sealMeters.r = held;
  reading.repaint(); sealMeters.repaint();
  const int h = proc.hits.load();
  if (h != lastHits) { lastHits = h; flashT = Time::getMillisecondCounterHiRes() / 1000.0; }
  const int c = proc.cur.load();
  const String wn = String(c + 1).paddedLeft('0', 2) + "  " + up(String::fromUTF8(kPresets[c].name));
  if (weather.getButtonText() != wn) weather.setButtonText(wn);
  repaint(590, 20, 26, 24);                // hit lamp
  repaint(LX + LW - 200, 316, 200, 36);    // note readout
  repaint(0, H - 34, W, 34);               // footer
}
} // namespace ui

//==============================================================================
UndertowEditor::UndertowEditor(UndertowProcessor& p) : AudioProcessorEditor(p), proc(p) {
  setLookAndFeel(&lnf);
  content = std::make_unique<ui::Content>(p, lnf);
  addAndMakeVisible(*content);
  proc.apvts.state.addListener(this);
  setResizable(true, true);
  setResizeLimits(ui::Content::W / 2, ui::Content::H / 2, ui::Content::W * 2, ui::Content::H * 2);
  if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)ui::Content::W / ui::Content::H);
  setSize(ui::Content::W, ui::Content::H);
}

UndertowEditor::~UndertowEditor() {
  proc.apvts.state.removeListener(this);
  content.reset();
  setLookAndFeel(nullptr);
}

void UndertowEditor::resized() { content->setTransform(AffineTransform::scale((float)getWidth() / (float)ui::Content::W)); }

void UndertowEditor::valueTreePropertyChanged(ValueTree&, const Identifier& id) {
  if (id.toString() == "ink") content->applyTheme();
}
