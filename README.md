# Feedback Stopper (VST3)

Automatic feedback suppressor: detects ringing as it builds and places narrow, self-releasing notch filters on it.
Original implementation (FFT peak tracking + adaptive notches), not a copy of any commercial plugin.

## Build

Needs CMake 3.22+, a C++17 compiler and internet access once (CMake downloads JUCE).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

- Windows: Visual Studio 2022 (Desktop C++ workload). macOS: Xcode command line tools. Linux: `libasound2-dev libx11-dev libxinerama-dev libxext-dev libxrandr-dev libxcursor-dev libfreetype-dev libfontconfig1-dev`.
- The VST3 is copied to your system VST3 folder automatically (`COPY_PLUGIN_AFTER_BUILD`). It is also in
  `build/FeedbackStopper_artefacts/<config>/VST3/`. A Standalone app is built alongside it for quick testing.
- If the JUCE tag in `CMakeLists.txt` (8.0.4) is unavailable, use any JUCE 8 release.
- Set `COMPANY_NAME` in `CMakeLists.txt` to your own name.

## Use

Insert it on a mic channel (mono or stereo). Raise the channel gain until it starts to ring; the plugin notches the
ringing frequencies within a fraction of a second. Notches show on the spectrum with their frequency and depth.

| Control | What it does |
|---|---|
| Sensitivity | Higher = reacts to weaker peaks (more false alarms). Default 5 |
| Cut per hit | Extra dB of cut each time the same frequency rings again |
| Max cut | Deepest a notch can go |
| Notch width Q | Higher = narrower notches, less tonal damage |
| Release after | Seconds a notch is held after its frequency goes quiet, then it fades out at 1 dB/s |
| Max notches | Number of simultaneous notches (up to 20 per channel) |
| Input / Output gain | Trim before and after the processing |
| Safety limiter | Soft limiter at the end of the chain (about -0.9 dBFS) to catch any surviving howl |
| Bypass | Fully transparent (no notches, no gain, no limiter) |

Zero added latency: it only measures the signal and applies minimum-phase notch filters.

## Layout

- `Source/FeedbackCore.h` - the whole DSP engine, no dependencies. Mono; the plugin runs one per channel.
- `Source/PluginProcessor.*`, `Source/PluginEditor.*` - JUCE wrapper and UI.
- `tests/core_test.cpp` - native test of the engine (simulated ringing room, growing tone, voice, voice + feedback):

      g++ -O2 -std=c++17 -ISource tests/core_test.cpp -o core_test && ./core_test

## Known limits

Detection is rule-based (peak prominence, persistence, growth rate, harmonic and onset guards), so very tonal
sustained sources such as a held organ note can occasionally be notched. Raise Cut per hit / lower Max cut, or use Bypass,
if that happens.
