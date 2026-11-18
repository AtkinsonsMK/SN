#!/bin/bash
# Builds dist/SN-Synth-<version>.pkg, which installs:
#   /Applications/SN Synth.app
#   /Library/Audio/Plug-Ins/VST3/SN Synth.vst3
#   /Library/Audio/Plug-Ins/Components/SN Synth.component (AU)
set -euo pipefail

ART="$1"
VERSION=$(grep -m1 'project(SNSynth VERSION' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')
STAGE=$(mktemp -d)
mkdir -p "$STAGE/Applications" \
         "$STAGE/Library/Audio/Plug-Ins/VST3" \
         "$STAGE/Library/Audio/Plug-Ins/Components" \
         dist

cp -R "$ART/Standalone/SN Synth.app"      "$STAGE/Applications/"
cp -R "$ART/VST3/SN Synth.vst3"           "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/SN Synth.component"        "$STAGE/Library/Audio/Plug-Ins/Components/"

# Always install to the standard locations, never "relocate" to an older copy.
COMP_PLIST=$(mktemp).plist
pkgbuild --analyze --root "$STAGE" "$COMP_PLIST"
count=$(/usr/libexec/PlistBuddy -c "Print" "$COMP_PLIST" | grep -c "BundleIsRelocatable" || true)
for ((i = 0; i < count; i++)); do
  /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$COMP_PLIST"
done

pkgbuild --root "$STAGE" \
         --component-plist "$COMP_PLIST" \
         --identifier com.snaudio.snsynth \
         --version "$VERSION" \
         --install-location / \
         "dist/SN-Synth-$VERSION.pkg"

echo "Built dist/SN-Synth-$VERSION.pkg"
