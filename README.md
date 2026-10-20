# Burnley 73 DSP reconstruction

This repository builds a VST3 effect with a double precision DSP core and a parameter-only host interface. It does not use VSTGUI or create an editor view.

## Build

Requirements: CMake 3.25 or newer, a C++17 compiler, and the Steinberg VST3 SDK. CMake fetches the pinned SDK revision by default. To use an existing SDK checkout:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBURNLEY73_VST3_SDK_ROOT=/path/to/vst3sdk
cmake --build build --target burnley73_dsp Burnley73
```

The VST3 bundle is written to `build/VST3/Release/Burnley73.vst3` (platform-specific binary under `Contents`). `SMTG_ENABLE_VSTGUI_SUPPORT` and SDK examples are disabled, and plugin symlinks are not created.

### Windows x64 cross-build

The current build environment has MinGW-w64 but no MSVC installation. The checked-in toolchain file targets 64-bit Windows:

```sh
cmake -S . -B build-win64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake-mingw64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBURNLEY73_VST3_SDK_ROOT=/path/to/vst3sdk \
  -DSMTG_RUN_VST_VALIDATOR=OFF \
  -DSMTG_CREATE_BUNDLE_FOR_WINDOWS=OFF
cmake --build build-win64 --target burnley73_dsp Burnley73
```

This produces the Windows x64 VST3 DLL at `build-win64/VST3/Burnley73.vst3`. MinGW's compiler runtimes, including winpthreads, are linked statically; its imports are limited to Windows system DLLs. The SDK validator is disabled for this cross-build because it is a Windows executable; the MinGW build itself was verified by inspecting the resulting PE32+ x86-64 binary. Wine could not run the SDK metadata utility successfully here, so this build has no Windows runtime/host validation.

## Controls

The host exposes 15 controls: High Pass, Low Shelv Freq/Gain/Power, Mid Freq/Gain/Power, High Shelv/Power, model selector (`Gain`), Output, EQ In, Phase invert, and Show Labels. There is no custom UI; the host's parameter controls provide the interface. The model selector spans values 0–20: `OFF` is 8, `Mic 5` is 9, and `Mic 60` is 20. Value 5 has no stored label; the implementation formats it as `5`, matching the binary's numeric fallback.

Output defaults to 0 dB plain value, matching the binary constructor. Under the recovered nonlinear mapping, that default is normalized `0.479879022`. Licensed runtime renders at that value and at two higher normalized points matched the native engine bit-for-bit in the tested bypass configuration.

## Reconstruction limits

- The supplied `data/license.reg` was imported into the dedicated `build-win64/rea-license-prefix`. REAPER 7.82 x64 under Wine 10 loaded the normal plug-in panel and produced activated references for bypass, EQ changes, Line 20, Mic 5, Mic 10, and Mic 60. A deterministic native-engine comparison of the 997 Hz Mic captures matches Mic 5 and Mic 10 within about 0.011 dB and correlation 0.999998; Mic 60 is within 0.043 dB and correlation 0.998871. The remaining Mic 60 residual is concentrated in near-Nyquist folded energy. A 48 kHz processing plus 44.1 kHz conversion simulation reduces it substantially across Mic 5, Mic 10, and Mic 60; the exact host setup rate, resampler, and thread-local CRT state remain unobserved. Direct licensed-original versus rebuilt Win64 renders now match four high-pass settings, six active mid frequencies at +9.6 dB, three further mid gains at 3.2 kHz, with additional EQ coverage across the same 44.1 kHz log sweep: low shelf across its full 0.1 dB gain domain at all four frequency choices (1,281/1,284 bit-identical; remaining error below −229 dB), and mid gain across its full domain at all six center frequencies (1,886/1,926 bit-identical; remaining error below −281 dB). High shelf has been swept at all 321 gain values from −16 to +16 dB at 0.1 dB steps: 198 settings are bit-identical, 118 differ only below −225 dB residual, and five positive-gain values (+1.4, +1.5, +1.6, +1.8, +2.3 dB) show repeatable −88 to −85 dB residuals that remain unresolved. Fresh REA confirms the frequency casts truncate toward zero, matching the port; exact runtime inputs/coefficient bytes at those five points remain to be captured. Full EQ control/rate coverage and deterministic transient parity remain outstanding.
- Mic reset accepts an explicit deterministic CRT `rand()` result and reproduces the observed recurrence loop. The native port mirrors the bundled CRT's per-thread sequence from `_holdrand=1`; which host thread performs each reset and whether its CRT state contributes to the remaining residual are unverified. The rate-conversion comparison shows that seed mismatch alone is not established as the cause.
- The parent's first-channel one-pole starts with the constructor coefficient `1 - exp(-2*pi) + 0.001867` (approximately `0.999999557268`) and survives sample-rate reset. The engine preserves that initial value. It does not reproduce the binary's delayed conditional mutations when its string/checksum configuration comparisons fail; those mutations are suppressed when all four checks pass, as observed in the normal licensed path.
- Model selector index 20 has no observed visible label; it is displayed as unresolved and maps through the Mic processing branch.
- The VST3 Boolean callback is not assigned a DSP bypass meaning. The DSP engine exposes a separate explicit block-bypass argument that copies audio and freezes its state.
- The binary has a `+0xbd0` guard that skips reset and copies audio when set. REA found the constructor initializes it from a shipped zero byte and found no in-module setter, so the port's unconditional reset matches the reachable path in this image; the conditional branch remains an external-state caveat, not a host control.

See [DOCUMENTATION.md](DOCUMENTATION.md) for the REA evidence and coefficient/model derivations, and [PROGRESS.md](PROGRESS.md) for the analysis history.
