// offline check without JUCE: g++ -O2 -std=c++17 src/test_render.cpp -o render && ./render <preset> <secs> <sr> out.raw
#include "undertow_params.h"
#include <cstdio>
#include <cstdlib>
using namespace undertow;
int main(int argc, char** argv) {
  int pi = argc > 1 ? atoi(argv[1]) : 0; double secs = argc > 2 ? atof(argv[2]) : 6, sr = argc > 3 ? atof(argv[3]) : 48000;
  const char* path = argc > 4 ? argv[4] : "out.raw"; const char* pat = argc > 5 ? argv[5] : "X...X...X...X...";
  static Engine e; e.prepare(sr);
  for (int i = 0; i < kNumParams; i++) applyParam(e.p, i, kPresets[pi].v[i]);
  e.p.bpm = 130;
  FILE* f = fopen(path, "wb"); int N = (int)(secs * sr); double stepLen = 60.0 / 130 / 4 * sr, toNext = 0; int st = 0;
  float L[1], R[1]; bool bad = false; float pk = 0;
  for (int i = 0; i < N; i++) {
    if (toNext <= 0) { char c = pat[st % 16]; if (c == 'X') e.trigger(1); else if (c == 'x') e.trigger(0.8); st++; toNext += stepLen; }
    toNext--;
    e.process(L, R, 1); if (!std::isfinite(L[0])) bad = true; pk = std::max(pk, std::abs(L[0]));
    fwrite(L, 4, 1, f); fwrite(R, 4, 1, f);
  }
  fclose(f); printf("%s peak %.3f %s\n", kPresets[pi].name, pk, bad ? "NaN!" : "");
}
