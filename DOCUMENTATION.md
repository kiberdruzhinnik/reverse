# Burnley73 DSP reconstruction

## Scope and status

This reference describes the DSP implemented by the VST3 reconstruction in `src/`. The target examined is the licensed Windows x64 plug-in at `data/Burnley73.vst3` (SHA-256 `a08cb9fb1a4636a51f7b366940d2d6dffebf696ee1fec5bc43484763a97f381a`). Its license registration was imported into the dedicated Wine prefix before host comparisons. REA/Ghidra analysis is static unless explicitly described as a runtime render; a decompilation alone does not prove output parity.

The reconstruction implements the parent processing path, six EQ sections, three model families, parameter conversion, smoothers, and model reset state. Parity is strong across many tested EQ settings and moderate-level model captures. It is not yet proven 1:1 for every control value, transient, host sample rate, or parameter transition. Five small but repeatable positive high-shelf gain outliers remain unresolved; see [Measured parity and open work](#measured-parity-and-open-work).

## Controls and value conversion

The processor exposes 15 automatable controls. VST3 parameter IDs start at `0x7300` in this order:

| Index | Control | Range or choices | Default (plain) |
|---:|---|---|---:|
| 0 | High Pass | Off, 50, 80, 160, 300 | Off |
| 1 | Low Shelv Freq | 35, 60, 110, 220 Hz | 110 Hz |
| 2 | Low Shelv Gain | −16 to +16 dB | 0 dB |
| 3 | Mid Freq | 360, 700, 1.6k, 3.2k, 4.8k, 7.2k Hz | 700 Hz |
| 4 | Mid Gain | −16 to +16 dB | 0 dB |
| 5 | High Shelv | −16 to +16 dB | 0 dB |
| 6 | Gain (model) | Line 40 down through Line 5, Off, Mic 5 through Mic 60 | Line 20 (index 4) |
| 7 | Output | −24 to +16 dB | 0 dB |
| 8–12 | High Pass Power, Low Shelv Power, Mid Power, High Shelv Power, EQ In | Off/On | On |
| 13 | Phase invert | Normal/Inverted | Normal |
| 14 | Show Labels | Off/On | On |

The model selector uses indices 0–7 for Line, 8 for the bypass model, and 9–20 for Mic. Index 5 has no model label in the binary and is displayed as `5`; index 20 is the Mic processing branch although its visible label was not resolved. The gain controls are continuous at the DSP input even though their displayed precision is 0.1 dB. Choice parameters round to the nearest declared index; booleans use a 0.5 threshold.

Output converts normalized host value `n` to plain dB as `−24 + 40 * n^p`, where `p = 0x1.6437c5692b3ccp−1` (about 0.695738). The inverse conversion raises `(dB + 24)/40` to `1/p`. Thus plain 0 dB is normalized about `0.479879022`, and the shipped default remains 0 dB. Licensed runtime renders at normalized `0.479879022`, `0.6`, and `0.8012644258244325` matched the native port bit-for-bit in the tested bypass setup; the latter values exercise the higher portion of the curve. This supersedes earlier notes that treated the first value as inconsistent with the constructor.

## Parent processing path

For each sample on the ordinary stereo path, the parent:

1. Updates 10 Hz smoothers for input model gain, Output gain, model post factor, and polarity.
2. Applies the model input factor and processes each channel through its selected model. In mono, the model runs once and its result is duplicated after the EQ path.
3. Applies an additional one-pole only to the first stream. Its constructor coefficient is `1 − exp(−2π) + 0.001867`; the smoother memory starts at zero and survives sample-rate reset.
4. Runs each stream through its independent EQ state.
5. Multiplies by Output gain, the model post factor, the fixed unity factor, and smoothed polarity, in that order.

The ordinary-path offset is zero and the fixed factor is one. The parent sample callback's current port matches the observed ordering and mono behavior (fresh REA Evidence `ev_866258d4f4631a2a3ca4efbb962ad0e8888071d722a78fff335b23cca6ed7949`, `rea-redo-parent-process.json`). The binary also contains a guarded protection path and an internal copy-bypass branch. The constructor initializes the guard from a shipped zero byte and no in-module setter was found; those branches are not ordinary user controls. The VST3 host bypass queue is handled separately from this model-selector bypass.

VST3 parameter queues are consumed at the final point in each queue before processing the block; the plugin does not split DSP at individual sample offsets. Float32 input is converted to double scratch, processed by the same double DSP, then converted back. Float64 uses direct double buffers. The plugin accepts mono or stereo and provides a simple host-parameter interface without VSTGUI or a custom editor.

## Shared arithmetic and state

The non-standard `binaryExp`, `binaryPower`, `binarySinCos`, and `binaryTan` helpers reproduce the target's table reductions and polynomial evaluation order over the finite DSP range. In particular, the exponential helper rounds its reduced table cell with `CVTPD2DQ`-compatible nearest-even behavior rather than truncating negative values. The source uses `-ffp-contract=off` for GCC/Clang and `/fp:strict` for MSVC so the observed scalar multiply/add boundaries are retained.

One-pole state updates use:

```text
state = (target - state) * alpha + 1e-25 + state
alpha = 1 - exp(-constant / sampleRate)
```

The `1e-25` history floor is part of the target's recurrence and is kept. Constructors initialize the parent smoother memories to zero; rate changes update coefficients while retaining smoother memories. EQ and model biquad histories are reset separately. The EQ recurrence uses a current-input-first sum, while the Line/Mic model recurrences add the delayed `b1*x1` term first; the mid EQ section also has its own feedback sign and operation order. These distinctions are implemented as separate `Biquad` methods.

## EQ processing

The EQ state has two high-shelf sections, two low-shelf sections, a high-pass section with a smoothed residual mix, and one peaking mid section. Its sample order is:

```text
high-shelf A/B with parallel residual mix
→ low-shelf A/B with parallel residual mix
→ high-pass plus smoothed residual mix
→ mid peaking section
```

The fresh REA cascade read confirms that the three shelf/high-pass recurrences subtract feedback while the final mid recurrence adds its stored feedback terms (Evidence `ev_a9c2ba22e8269529fef23b68425e8d7d743a0033af6bc4e85fcc62f6922c18e3`, `rea-redo-eq-cascade.json`). The shared coefficient builder's case 3 uses the observed high-shelf `A`, frequency angle, alpha, normalization denominator, and five coefficient writes; fresh builder Evidence is `ev_58bdc7f646b07f919901fb1c37890f946f08f7a85ff205005d8110ff5ef52413` in `rea-redo-biquad-builder.json`.

### High pass

The High Pass choices map to biquad frequency/Q pairs `(6 Hz, 0.99)`, `(32 Hz, 2.29)`, `(52 Hz, 2.29)`, `(139 Hz, 2.5)`, and `(281 Hz, 2.746)`. Each also has a model-specific residual mix and a 20–564 Hz pole smoother. Power-off selects the index-zero 6 Hz path. An enabled malformed nonzero enum uses the target's separate 1 Hz / Q 2.29 path with no residual mix; normal public values cannot select it. The section uses the observed RBJ-style coefficient equations and keeps the original multiply/divide order for the two half-scaled feed-forward coefficients.

### Low and high shelves

The shelf handlers interpolate shipped tables using closed interval checks and a weighted expression whose evaluation order is retained in `src/eq.cpp`; outside a matching interval the target's fallback is 0.1. Negative and positive gains use different tables and section parameters. Low-shelf gain is multiplied by 0.9375 before its sign-specific branches. Positive low-shelf indices have dedicated table paths, frequency scales, and fixed second-section settings. High-shelf negative gain uses `x = 1.125 * gain` and separate tables. Positive high-shelf gain uses a polynomial `T(gain)`, a table interpolation for section A and a second coordinate, then another table lookup for section B. The exact table values are in `src/eq_tables.inc`.

The shared high-shelf builder computes `A = 10^(gainDb/40)`, `w = frequency * 2π / sampleRate`, `alpha = sqrt(A) * 2 * (sin(w)/(shape*2))`, followed by the target's stored coefficient ordering. Low shelf follows its corresponding low-shelf numerator/denominator equations. EQ In off installs the dedicated 6 Hz / Q .99 high-pass and rebuilds the shelf/mid gains at zero while preserving the selected frequency indices.

### Mid section

Mid gain first passes through a sign-specific nonlinear transform and table lookup. The six frequency indices select different center-frequency and bandwidth factors; positive gain applies an additional per-index scale. `buildMid` derives its coefficients from the transformed gain, bandwidth, center frequency, tangent terms, and the target's square-root expressions. A near-zero gain installs identity. The mid section's recurrence adds `a1*y1 + a2*y2`, unlike the other EQ sections; this was required to prevent instability and match the shipped processor.

### EQ runtime coverage

At 44.1 kHz, licensed-original versus rebuilt Win64 VST3 sweeps established bit-identical low-shelf responses at all four frequency selections for gains −16, −9.6, 0, +9.6, and +16 dB. A dense 0.1 dB low-shelf sweep had 1,281/1,284 bit-identical pairs; the other three residuals were below −229 dB. A dense mid sweep over all six center frequencies had 1,886/1,926 bit-identical pairs; the remaining residuals were below −281 dB. High-pass tests covered the four active choices and matched at approximately −159.9 dB residual on the captured tone. A 20 Hz–20 kHz log sweep matched selected shelf settings, including both signs at low shelf and high shelf.

The dense high-shelf sweep is the remaining EQ mismatch: 198/321 gain settings were bit-identical; 118 of the other 123 were below −225 dB residual. At +1.4, +1.5, +1.6, +1.8, and +2.3 dB, repeatable residuals ranged from −88.09 to −85.04 dB. A separate original-versus-port render confirmed the +1.4 dB outlier. REA shows that both sides truncate the final frequency conversion toward zero, so the mismatch occurs earlier. Live original builder arguments at RVA `0x137b0` showed section-A frequencies 482/481/482/481/482 Hz for those five gains, while the local port produced 481/482/481/482/481 Hz. The builder gain arguments also differ by a few ULPs (about five ULPs at +1.4 dB). Do not apply a gain-specific correction: the upstream rounding source and section-B coefficients still need to be captured. See `build-win64/tmp/high-shelf-dense-gain-20261020.tsv`, handler Evidence `ev_2109e4c402969e14400ea04351dbf92a0342bb7ccae53262d8c467cc0428108c`, and live +2.3 dB frequency/gain Evidence `ev_f1fa70a5549a580f012716bd8f864f366521015ee2f3aefb5c98cad3dc04eea4` / `ev_5e19d2eac2d7b6ce5aac1de57236632d3f4ab95e0ec94724faa0658b`.

## Model processing and saturation

Selectors 0–7 run Line, selector 8 runs the dedicated bypass model, and selectors 9–20 run Mic. Both model biquads use the delayed-first recurrence and add the `1e-25` floor to their delayed input history. Sample-rate reset clears filter histories and selected feedback state, updates alphas, and leaves smoother memories intact. Mic reset also advances a private recurrence `rand()%0x37aa0 + 1` times from state 1; the CRT draw that selects this count is supplied explicitly by the port because the target's call is thread-local and host-history dependent.

### Line models

All Line modes begin with a low-passed input and a fixed high-frequency lift `input + 0.083*(input-low)`, then pass through a reset-time high-shelf filter and its smoother. Per-mode input scaling is `{1.5, .9, .6, .3, .2, .15, .1, .07}`. The feedback path tracks the squared envelope and filtered input, then forms `u = 0.4*(q + v - 0.1*envelope)`. Its bounded rational shaper is:

```text
rational(x) = x / (((x * 0x1.7afffa1aeb983p-2) * x)
                   + 0x1.291a64c75f379p+0)
              + x*0x1.b6ae8f5980a32p-4
```

The result clamps at input magnitude `0x1.75de5deec8a01p+1` to output magnitude `0x1.fa7cd209d62e2p-1`. The shaper updates feedback; slow and residual feedback smoothers form the residual output, which is low-passed and multiplied by the mode's output scale and final target. Fresh Line processor Evidence `ev_4749950d134de09866b6e6bb671e7753a947ab651ff6c6008bbae04e71086249` (`rea-redo-line-process.json`) matches this state and update order to the port.

The Line reset/model configuration and all eight mode constants are in `src/model.cpp`. Rate groups at or below 44.1 kHz, 48 kHz, 88.2 kHz, 96 kHz, and above 96 kHz select distinct reset shelf, pole, and output coefficients. The stored 22.5 kHz target applies through 96 kHz; 23.5 kHz is used above 96 kHz.

### Mic models

Mic mode scales derive from `delta = (selectorIndex−9)*2.6`: input factor is `exp(delta*0.1151292546497)` and post factor is `exp(delta*−0.7*0.1151292546497)`. The sample path high-passes the input, runs the shared rate-shaped filter A, advances a private linear congruential random state, filters four correlated random modulation states, and runs filter B. It then forms a nonlinear drive from `0.55*t*t−1`, updates two smoothed envelope powers from the prior shaper output, and computes feedback from the residual, filtered low/high feedback, and envelope term.

The asymmetric Mic waveshaper uses separate positive/negative cubic and fifth-order coefficients. For `z`, it forms `z3=(z*z)*z` and `w=((z3*c3)*m)+z+((((z3*z)*z)*c5)*m)`, where `m=max(magnitude,0)`. It clamps `w` at the same limits as Line, then applies the bounded rational shaper. The new shaper output updates drive subtraction, low-frequency output, two high-pass feedback paths, a fixed-mix smoother, and the final output smoother. The routine's state and arithmetic order match fresh REA Evidence `ev_88fdb9d22c10f9ee2d24e047b11df352519bf4ea53361a2cd830aab4a58170b8` (`rea-redo-mic-process.json`).

The target's Windows CRT random generator uses `holdrand = holdrand*0x343fd + 0x269ec3` and returns `(holdrand >> 16) & 0x7fff`. Parent construction time-seeds the current thread and consumes thousands of draws before Mic setup. The exact host thread and initial timestamp affect reset state. Controlled steady-tone comparisons found seed changes small for those captures, but matched random startup across hosts and deterministic transient parity are not fully established.

### Bypass model

Selector 8 still processes audio. It applies `input + 0.03*(input-low)` and a dedicated high-shelf section whose coefficient path differs from the shared EQ builder. Its fresh sample routine was previously captured as Evidence `ev_5159fe699666a03008b5576202055b509a883ff6c717cb1fb22e6882ead47896`; reset Evidence is `ev_a6279251d68197edae959bfca741bb21494e5c1fe7f29a79772bca32d00eec4a`. A separate block-bypass request copies audio and freezes DSP state.

## Measured parity and open work

Runtime evidence uses licensed-original and reconstructed VST3 renders in REAPER or a local Wine VST3 harness, with channel alignment and paired parameter values recorded in `PROGRESS.md`. No REAPER MCP was used. At 44.1 kHz, captured 997 Hz Mic 5/10/60 tones showed close deterministic output; Mic 60's remaining error was concentrated in near-Nyquist folded harmonics. Additional paired Mic sine and noise captures at 44.1/48 kHz approached the original's run-to-run variation. A four-second Mic 60 impulse's late modulation tail also varied between original runs, so it is not a clean deterministic parity measure without matched reset randomness.

Current unresolved work:

1. Trace the upstream high-shelf polynomial/table interpolation rounding that makes the five positive-gain integer-frequency boundaries differ; capture original section-B arguments and coefficient bytes, then rerender the full 321-point sweep.
2. Expand EQ settings and sample-rate coverage, particularly automation and state transitions where retained smoother/filter histories matter.
3. Establish deterministic Mic reset/random thread state across the VST3 host boundary, then compare transient and broadband response over time.
4. Re-run end-to-end checks on the final rebuilt binaries after any parity fix. Static REA agreement and selected passing renders do not establish complete 1:1 behavior.

## REA evidence for the restored core analysis

The following complete REA/Ghidra function records were captured against the supplied original plug-in while rebuilding this document:

| Target routine | Evidence | Saved record |
|---|---|---|
| Parent stereo/mono sample callback `FUN_18000f6a0` | `ev_866258d4f4631a2a3ca4efbb962ad0e8888071d722a78fff335b23cca6ed7949` | `rea-redo-parent-process.json` |
| EQ cascade `FUN_180013090` | `ev_a9c2ba22e8269529fef23b68425e8d7d743a0033af6bc4e85fcc62f6922c18e3` | `rea-redo-eq-cascade.json` |
| High-shelf handler `FUN_180011e40` | `ev_2109e4c402969e14400ea04351dbf92a0342bb7ccae53262d8c467cc0428108c` | `rea-current-highshelf-handler-followup.json` |
| Shared biquad builder `FUN_1800112b0` | `ev_58bdc7f646b07f919901fb1c37890f946f08f7a85ff205005d8110ff5ef52413` | `rea-redo-biquad-builder.json` |
| Line sample routine `FUN_1800152c0` | `ev_4749950d134de09866b6e6bb671e7753a947ab651ff6c6008bbae04e71086249` | `rea-redo-line-process.json` |
| Mic sample routine `FUN_180016a90` | `ev_88fdb9d22c10f9ee2d24e047b11df352519bf4ea53361a2cd830aab4a58170b8` | `rea-redo-mic-process.json` |

Earlier supporting records for parameter conversion, reset/setup, nonlinear helpers, low-shelf/mid handlers, the bypass path, and runtime argument captures remain in the adjacent `rea-*.json` files and are indexed chronologically in `PROGRESS.md`.
