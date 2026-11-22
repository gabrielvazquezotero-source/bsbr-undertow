#!/bin/bash
# Instala bsbr undertow (VST3 + AU) en tu Mac. Doble clic para ejecutar.
cd "$(dirname "$0")"
echo ""
echo "  bsbr · undertow  -  instalando..."
echo ""
mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
rm -rf ~/Library/Audio/Plug-Ins/VST3/undertow.vst3 ~/Library/Audio/Plug-Ins/Components/undertow.component
cp -R undertow.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -R undertow.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/undertow.vst3 2>/dev/null
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/undertow.component 2>/dev/null
killall -9 AudioComponentRegistrar 2>/dev/null
echo "  Listo. Abre (o reinicia) Ableton Live:"
echo "  Preferencias > Plug-ins > activa VST3 / Audio Units y pulsa Rescan."
echo "  Aparece en Plug-ins > bsbr > undertow (instrumento)."
echo ""
read -n 1 -s -r -p "  Pulsa cualquier tecla para cerrar..."
