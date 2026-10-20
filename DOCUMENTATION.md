# Burnley73 DSP reconstruction

## Scope and status

This reference describes the DSP implemented by the VST3 reconstruction in `src/`. The target examined is the licensed Windows x64 plug-in at `data/Burnley73.vst3` (SHA-256 `a08cb9fb1a4636a51f7b366940d2d6dffebf696ee1fec5bc43484763a97f381a`). Its license registration was imported into the dedicated Wine prefix before host comparisons. REA/Ghidra analysis is static unless explicitly described as a runtime render; a decompilation alone does not prove output parity.

The reconstruction implements the parent processing path, six EQ sections, three model families, parameter conversion, smoothers, and model reset state. Parity is strong across many tested EQ settings and moderate-level model captures. It is not yet proven 1:1 for every control value, transient, host sample rate, or parameter transition. The five formerly large positive high-shelf errors were fixed by correcting section A's nested frequency lookup. The dense sweep still has tiny residuals at 118 positive-gain settings; see [Measured parity and open work](#measured-parity-and-open-work).

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

The generic normalized-to-plain setter first evaluates the parameter's stored exponent with the binary's power routine, scales the result by `(maximum - minimum)`, adds `minimum`, and stores that value. It only enters the step-quantization branch when the parameter type tag is not `4`; the three EQ gain parameters use type `4`, so their displayed `0.1 dB` step does not quantize the DSP value. The final stored value is capped at the parameter maximum. The decompiled instruction order is `pow(normalized, exponent) * range + minimum`. Direct calls into the original binary's setter and power helper matched the port for all 321 normalized gain points in the 44.1 kHz dense sweep. At reconstructed positive high-shelf section inputs, original and port sine/cosine helpers also matched exactly for all 322 calls. The section power helper differed by one ULP at only one of 322 calls: section A at requested +10.4 dB, where the interpolated section gain is `-4.348... dB`; all other tested section-power calls matched. These isolated helper checks narrow the remaining sub-`1e-12` render residuals but do not yet identify their cause; the handler's queued runtime inputs and full coefficient values have not been captured. REA Evidence: `ev_d3fcd28f8701359af704a2d6c6f705f8f74fd7e727951d13d37a9e86c0745283` (`FUN_18001bcb0`, `rea-current-normalized-to-plain-conversion-20261020.json`), `ev_7549cd8a655a7bae7da79921994d12cfe5a60d91405bb972efc8450618b39017` (`FUN_1800ee000`, binary power helper, `rea-current-parameter-range-interpolation-20261020.json`), sine `ev_3c33fe851ddff95178ed38737fd3ec2e34d0a06b0fa77c9e49141f01338e4099`, and cosine `ev_d69e747b6d31114580562f7e1ae9c615e37a5a6cb1e4250af198c0ddff0e5a45`.

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

The non-standard `binaryExp`, `binaryPower`, `binarySinCos`, and `binaryTan` helpers reproduce the target's table reductions and polynomial evaluation order over the active DSP range. In particular, the exponential helper rounds its reduced table cell with `CVTPD2DQ`-compatible nearest-even behavior rather than truncating negative values. A fresh comparison of the original sine and cosine helpers found that the large-exponent-gap quadrant correction must subtract the low `π/2` term from the already high-part-reduced angle. `binarySinCos` previously applied that correction to the unreduced input; it now uses the reduced angle, matching the target's `FUN_1800ef6d0` and `FUN_1800ef240` pseudocode (Evidence `ev_3c33fe851ddff95178ed38737fd3ec2e34d0a06b0fa77c9e49141f01338e4099`, `rea-current-sine-helper.json`; `ev_d69e747b6d31114580562f7e1ae9c615e37a5a6cb1e4250af198c0ddff0e5a45`, `rea-current-cosine-helper.json`). This correction targets angles close to a quadrant boundary; the 44.1 kHz high-shelf sweep's builder angles remain below `π/4`, so it does not explain that sweep's remaining tiny residuals. The source uses `-ffp-contract=off` for GCC/Clang and `/fp:strict` for MSVC so the observed scalar multiply/add boundaries are retained.

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

The shelf handlers interpolate shipped tables using closed interval checks and a weighted expression whose evaluation order is retained in `src/eq.cpp`; outside a matching interval the target's fallback is 0.1. Negative and positive gains use different tables and section parameters. Low-shelf gain is multiplied by 0.9375 before its sign-specific branches. Positive low-shelf indices have dedicated table paths, frequency scales, and fixed second-section settings. High-shelf negative gain uses `x = 1.125 * gain` and separate tables. Positive high-shelf gain computes a polynomial `T(gain)`, interpolates a coordinate from the `0x1800d4040` and `0x1800d42d0` tables, then uses that coordinate for section A's frequency lookup (`0x1800d42d0` → `0x1800d40e0`) and section B's gain, frequency, and shape lookups. Section A's gain is interpolated directly from `T` (`0x1800d4040` → `0x1800d4180`). This nested section-A frequency lookup is implemented in `src/eq.cpp` and confirmed by fresh REA pseudocode for `FUN_180011e40` (Evidence `ev_7fc6108952a4e5517dfa2ac342f4b3e7bdd679451d755f76dc9586d582fced94`, `rea-current-highshelf-handler-confirmed-20261020.json`). The exact table values are in `src/eq_tables.inc`.

The shared high-shelf builder computes `A = 10^(gainDb/40)`, `w = frequency * 2π / sampleRate`, `alpha = sqrt(A) * 2 * (sin(w)/(shape*2))`, followed by the target's stored coefficient ordering. A fresh complete REA read of `FUN_1800137b0` confirms the port's scalar expression order and coefficient slots for `a1`, `a2`, `b0`, `b1`, and `b2`; the target's `FUN_18008fe10` helper takes the hardware `SQRT` path for positive finite `A`. The rebuilt Win64 object emits `SQRTSD` for the corresponding `std::sqrt`, so this operation matches at the instruction level. These checks reduce the likelihood that the shelf coefficient builder explains the tiny residuals, but do not prove every output sample bit-identical. Evidence: `ev_b4d876b7ef911b382951a502b8e65b31ad2a9df5d3abc0eae36071e3238e8638` (`rea-current-eq-shelf-builder-20261020.json`) and `ev_90f4999489c969e79bb636aa1f6e8da6a5fc97c873288165925f588442a76f49` (`rea-current-eq-sqrt-helper-20261020.json`). Low shelf follows its corresponding low-shelf numerator/denominator equations. EQ In off installs the dedicated 6 Hz / Q .99 high-pass and rebuilds the shelf/mid gains at zero while preserving the selected frequency indices.

### Mid section

Mid gain first passes through a sign-specific nonlinear transform and table lookup. The six frequency indices select different center-frequency and bandwidth factors; positive gain applies an additional per-index scale. `buildMid` derives its coefficients from the transformed gain, bandwidth, center frequency, tangent terms, and the target's square-root expressions. A near-zero gain installs identity. The mid section's recurrence adds `a1*y1 + a2*y2`, unlike the other EQ sections; this was required to prevent instability and match the shipped processor.

### EQ runtime coverage

At 44.1 kHz, licensed-original versus rebuilt Win64 VST3 sweeps established bit-identical low-shelf responses at all four frequency selections for gains −16, −9.6, 0, +9.6, and +16 dB. A dense 0.1 dB low-shelf sweep had 1,281/1,284 bit-identical pairs; the other three residuals were below −229 dB. A dense mid sweep over all six center frequencies had 1,886/1,926 bit-identical pairs; the remaining residuals were below −281 dB. High-pass tests covered the four active choices and matched at approximately −159.9 dB residual on the captured tone. A 20 Hz–20 kHz log sweep matched selected shelf settings, including both signs at low shelf and high shelf.

After correcting section A to use the target's nested frequency lookup, a fresh 321-point 0.1 dB sweep at 44.1 kHz produced bit-identical output at all five former outliers (+1.4, +1.5, +1.6, +1.8, and +2.3 dB), across 352,800 interleaved samples per setting. Overall, 203/321 settings were bit-identical; the remaining 118 had residuals from −232.24 to −229.79 dB and maximum peak error below `8.84e−13`. This removes the earlier −88 to −85 dB outliers, while small floating-point differences remain at some other positive gains. The licensed-versus-port measurements are recorded in `high-shelf-dense-gain-postfix-20261020.json`.

Fresh REA pseudocode for `FUN_180011e40` confirms the section-A frequency interpolation uses the intermediate coordinate as the next lookup input (Evidence `ev_7fc6108952a4e5517dfa2ac342f4b3e7bdd679451d755f76dc9586d582fced94`). A fresh read of dispatcher `FUN_18000fc80` confirms indices 5 and 11 trigger high-shelf recalculation for the two channel states and pass values loaded from the selected parameter objects' `+8` field (Evidence `ev_2d72a2d07d2ee37a571d4306f5752a814fe72bedb982215dfc19aa6a35c89d77`, `rea-current-parameter-dispatch-confirmed-20261020.json`). The identity of the setter and the exact stored input bits remain unresolved; the other 118 tiny residuals are open. Main target overview Evidence is `ev_19eeb42f4e4b250a13682b092df341be1197f5ae1760fbca37e76543ae2c46a` (`rea-current-overview-20261020.json`), matching the shipped VST3 SHA-256 recorded above.

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

1. Capture the exact queued normalized high-shelf gain bits and handler interpolation intermediates for representative settings among the 118 tiny-residual gains. The generic setter confirms continuous, unsnapped EQ gains, and the shelf builder's finite coefficient path matches the port's operation order; the remaining sub-`1e-12` differences are therefore more likely to enter through upstream values or trigonometric helpers, but this remains to be tested.
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
