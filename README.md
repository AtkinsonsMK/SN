# SN Synth

A small analogue-style subtractive synthesizer for macOS, as a standalone app
and as a VST3 / AU plugin for FL Studio and other DAWs.

**Status: step 2 of 4.** The sound engine: one oscillator (Saw, Pulse, Sub),
filter and amp envelopes, an LFO, Drift and Glide, 8 voices, and an on-screen
keyboard you can play from your computer keyboard. A simple temporary filter
is in place until the filter rack arrives in step 3.

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

- **Computer keyboard** (standalone app, or with the plugin window focused):
  `A W S E D F T G Y H U J K O L P ;` play the notes from C upwards;
  **Z / X** shift down / up an octave.
- **MIDI keyboard**: in the standalone app, click **Options** to choose your
  MIDI input and audio output. In FL Studio, MIDI comes through FL as usual.

## Controls

Every control can be automated in FL Studio. The full list of controls, with
ranges and defaults, is in the project's *Synth sound concept and parameters*
document.

| Group | Control | Range | Default |
|---|---|---|---|
| Oscillator | Saw Level | 0 to 100% | 100% |
| | Pulse Level | 0 to 100% | 0% |
| | Pulse Width | 5 to 95% | 50% |
| | Sub Level | 0 to 100% | 0% |
| Filter Envelope | Attack | 0.5 ms to 10 s | 1 ms |
| | Decay | 1 ms to 10 s | 400 ms |
| | Sustain | 0 to 100% | 30% |
| | Release | 1 ms to 15 s | 300 ms |
| Amp Envelope | Attack | 0.5 ms to 10 s | 1 ms |
| | Decay | 1 ms to 10 s | 300 ms |
| | Sustain | 0 to 100% | 100% |
| | Release | 1 ms to 15 s | 200 ms |
| Movement | LFO Rate | 0.05 to 20 Hz | 4 Hz |
| | LFO to Cutoff | 0 to 100% | 0% |
| | Drift | 0 to 100% (up to about ±10 cents) | 15% |
| | Glide | 0 to 2 s | 0 s |
| Output | Volume | -inf to +6 dB | -6 dB |

## Building from source

Requires CMake 3.22+ and Xcode command-line tools. JUCE is downloaded
automatically.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DSN_BUILD_TESTS=ON
cmake --build build --config Release --target SNSynth_All SNSynthTests
./build/SNSynthTests
```

Every push to `main` builds the Mac installer on GitHub Actions.
