# bsbr · undertow (VST3 + AU)

Instrumento de bombo + rumble: generador de kick, beater, notch, 5 drives (Mack, Valve, Sine, Analog, Chanel + Gale),
haze (reverb brillante con drift), echo sincronizado, OTT y duck por rama, clip/limit y el **bsbr seal**. 16 presets.

## Descargar (sin programar nada)
Cada vez que se sube un cambio, GitHub compila el plugin solo (pestaña **Actions**, unos 10 minutos)
y lo publica en **Releases** (columna derecha de la página del repo):
1. Releases > la versión más nueva > descarga `bsbr-undertow-mac.zip`.
2. Descomprime y haz doble clic en **Instalar undertow.command** (si macOS lo bloquea: clic derecho > Abrir).
3. Ableton Live > Preferencias > Plug-ins > Rescan. Aparece en Plug-ins > bsbr > undertow.

## Para desarrollo
- `src/undertow_engine.h`: motor de audio (port 1:1 del prototipo web, sin JUCE).
- `src/undertow_params.h`: parámetros y presets (generado desde el prototipo).
- `src/test_render.cpp`: prueba sin JUCE: `clang++ -O2 -std=c++17 src/test_render.cpp -o render && ./render 0 4 48000 out.raw`.
- Compilar a mano: `cmake -B build -G Xcode && cmake --build build --config Release`.
