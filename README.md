# SN Synth

A small analogue-style subtractive synthesizer for macOS, as a standalone app
and as a VST3 / AU plugin for FL Studio and other DAWs.

**Status: step 4 of 4.** Live displays (scope, filter curves, envelope and
LFO shapes, EQ curve) and a Numbers window showing the sample values at each
stage. Under the hood: One oscillator (Saw, Pulse, Sub), filter and amp
envelopes, an LFO, Drift and Glide, a three-slot filter rack (all seven types), a 3-band EQ, 8 voices, and an on-screen keyboard you
can play from your computer keyboard. 38 controls in all.

## Install (Mac)

1. Download the latest `SN-Synth-x.y.z.pkg` from the **Releases** page (or from
   the latest run on the **Actions** tab, under "Artifacts").
2. Double-click it. macOS will say it can't check the developer, because the
   installer isn't signed with a paid Apple developer account. Open
   **System Settings > Privacy & Security**, scroll down and click
   **Open Anyway**, then run the installer.
3. It installs:
   - **SN Synth** app in Applications
   - VST3 in `/Library/Audio/Plug-Ins/VST3`
   - AU in `/Library/Audio/Plug-Ins/Components`

### FL Studio

Options > Manage plugins > **Find installed plugins**, then add **SN Synth**
from the plugin list (as a generator). Either the VST3 or the AU version works.

## Playing

- **Computer keyboard** (FL Studio layout, two rows):
  lower octave `Z S X D C V G B H N J M , L . ; /` (Z = C3),
  upper octave `Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P [ = ]` (Q = C4).
  The - / + buttons beside the keyboard move both rows by an octave.
  Notes keep playing while you move knobs and menus with the mouse.
- **MIDI keyboard**: in the standalone app, click **Options** to choose your
  MIDI input and audio output. In FL Studio, MIDI comes through FL as usual.

## Controls

Every control can be automated in FL Studio. The full list of controls, with
ranges and defaults, is in the project's *Synth sound concept and parameters*
document.

| Group | Control | Range | Default |
|---|---|---|---|
| Oscillator 1 | Sine | 0 to 100% | 0% |
| | Saw | 0 to 100% | 100% |
| | Pulse | 0 to 100% | 0% |
| | Pulse Width | 5 to 95% | 50% |
| | Coarse (tuning in semitones) | -24 to +24 st | 0 st |
| | Fine (tuning in cents, 100 = one semitone) | -100 to +100 ct | 0 ct |
| | Level (how much goes to the mix) | 0 to 100% | 100% |
| Oscillator 2 | Sine | 0 to 100% | 0% |
| | Saw | 0 to 100% | 100% |
| | Pulse | 0 to 100% | 0% |
| | Pulse Width | 5 to 95% | 50% |
| | Coarse | -24 to +24 st | 0 st |
| | Fine | -100 to +100 ct | +7 ct (slight detune for width) |
| | Level | 0 to 100% | 0% (off, so older sounds don't change) |
| Mix and Voices | Sub (square one octave below Osc 1) | 0 to 100% | 0% |
| | FM 1 > 2 (Osc 1 bends Osc 2's wave: metallic, bell-like) | 0 to 100% | 0% |
| | FM 2 > 1 (Osc 2 bends Osc 1's wave) | 0 to 100% | 0% |
| | Ring Mod (Osc 1 × Osc 2: clangy, bell-like) | 0 to 100% | 0% |
| | Voices (notes at once; 1 = Mono, legato, newest key wins) | Mono, 2 to 16 | 8 |
| Filter Envelope | Attack | 0.5 ms to 10 s | 1 ms |
| | Decay | 1 ms to 10 s | 400 ms |
| | Sustain | 0 to 100% | 30% |
| | Release | 1 ms to 15 s | 300 ms |
| Amp Envelope | Attack | 0.5 ms to 10 s | 1 ms |
| | Decay | 1 ms to 10 s | 300 ms |
| | Sustain | 0 to 100% | 100% |
| | Release | 1 ms to 15 s | 200 ms |
| Mix and Voices | Drift (slow random pitch wander per note) | 0 to 100% (up to about ±10 cents) | 15% |
| | Glide (slide between notes) | 0 to 2 s | 0 s |
| Playing | Velocity (how much playing harder makes notes louder) | 0 to 100% | 50% |
| | Key Track (how much filter cutoff follows the note you play) | 0 to 100% | 50% |
| | Bend Range (pitch wheel) | 0 to 24 semitones | 2 st |
| LFO 1 | Rate | 0.05 to 20 Hz | 4 Hz |
| | Shape | Sine, Triangle, Saw Up, Saw Down, Square, Random | Triangle |
| | Smooth (rounds off sharp corners and steps) | 0 to 100% | 0% |
| LFO 2 | Rate | 0.05 to 20 Hz | 0.5 Hz |
| | Shape | as LFO 1 | Sine |
| | Smooth | 0 to 100% | 0% |
| Filter 1, 2, 3 | Type | Off, OTA, Sallen-Key, Lowpass Gate, Steiner-Parker, Polivoks, Comb, Formant | OTA / Off / Off |
| | Cutoff | 20 Hz to 20 kHz | 5 kHz / 2 kHz / 2 kHz |
| | Resonance | 0 to 100% | 15% |
| | Drive | 0 to 100% | 0% |
| | Env Amount | -100 to +100% | +40% / 0% / 0% |
| | Match (Level Match: evens out loudness between filter types) | on / off | on |
| | Type knob | OTA: Slope 24/12 dB; Sallen-Key: HP Cutoff 20 Hz to 2 kHz; Lowpass Gate: Gate Decay 0.12 to 3 s; Steiner-Parker: Mode LP/BP/HP; Polivoks: Mode LP/BP; Comb: Polarity +/-; Formant: Vowel A/E/I/O/U (Cutoff 1 kHz = natural voice) | 0% |
| EQ | Low (shelf 100 Hz) | -12 to +12 dB | 0 dB |
| | Mid (bell 1 kHz) | -12 to +12 dB | 0 dB |
| | High (shelf 8 kHz) | -12 to +12 dB | 0 dB |
| Output | Volume | -inf to +6 dB | -6 dB |
| | Limiter (keeps peaks just under 0 dB, smoothly) | on / off | on |
| | Voice Gain Comp (turns chords down so they don't jump in level) | on / off | on |
| | Filter oversampling (filters run 1, 2 or 4 times faster inside: cleaner at high resonance/drive, more CPU) | 1x, 2x, 4x | 2x |

**Safety:** whatever the settings, nothing leaves the synth above -0.3 dB. With the Limiter off, loud peaks are clipped flat instead (the CLIP light shows when that happens).

## Modulation

Any knob with a coloured ring can be moved by a modulation source:
LFO 1, LFO 2, Filter Env, Amp Env, Velocity, Mod Wheel, or Key (which note you play; middle C is the centre).

1. Click a source at the top (**MODULATE WITH**). Knobs that can take it get a faint ring.
2. Drag any knob up or down to set how much the source moves it (hold Shift for fine control). A ring in the source's colour shows the range.
3. Double-click a knob to disconnect that source. Click the source again when you're done.

The **Modulation** panel lists every connection with its amount; click × to remove one. Amounts are a share of the knob's full travel (for Cutoff, 100% = 10 octaves). Connections are saved with the project.

The **output meter** next to Volume shows the level in dB: the solid bar is the average, the thin line is the peak, and the white line holds the recent peak. The **CLIP** light turns red if the sound goes over 0 dB and stays on until you click it.

The panel scales: it opens at a size that fits your screen, and you can drag the bottom-right corner to resize it.

Filter slots run in series; the < > buttons on each slot change the order,
which is saved with the project.

## Building from source

Requires CMake 3.22+ and Xcode command-line tools. JUCE is downloaded
automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSN_BUILD_TESTS=ON
cmake --build build --config Release --target SNSynth_All SNSynthTests
./build/SNSynthTests
```

Every push to `main` runs the offline checks on GitHub Actions (Linux). The Mac installer build on GitHub runs only when started by hand or for a version tag; day to day the app is built on the Mac itself.
