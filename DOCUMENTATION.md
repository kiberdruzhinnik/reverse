# Burnley73 DSP reconstruction

## Scope and status

This reference describes the DSP implemented by the VST3 reconstruction in `src/`. The target examined is the licensed Windows x64 plug-in at `data/Burnley73.vst3` (SHA-256 `a08cb9fb1a4636a51f7b366940d2d6dffebf696ee1fec5bc43484763a97f381a`). Its license registration was imported into the dedicated Wine prefix before host comparisons. REA/Ghidra analysis is static unless explicitly described as a runtime render; a decompilation alone does not prove output parity.

The reconstruction implements the parent processing path, six EQ sections, three model families, parameter conversion, smoothers, and model reset state. It is not yet proven 1:1 for every sample, transient, host sample rate, or parameter transition. The former large high-shelf errors were fixed by correcting section A’s nested lookup, and the remaining one-ULP coefficient mismatch was fixed by matching the custom power helper’s polynomial addition tree. Direct original-handler coefficient parity now holds across 321 gains at 44.1, 48, and 96 kHz; the earlier end-to-end audio sweep still has 118 tiny residual settings from a pre-fix build and must be rerun. See [Measured parity and open work](#measured-parity-and-open-work).

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

The model selector uses indices 0–7 for Line, 8 for the bypass model, and 9–20 for Mic. Index 5 has no model label in the binary and is displayed as `5`; registration code assigns index 20 the visible label `Mic 60`. The gain controls are continuous at the DSP input even though their displayed precision is 0.1 dB. Choice parameters round to the nearest declared index; booleans use a 0.5 threshold.

The processor also recognizes a special `prst` parameter queue (`0x70727374`) and forwards its value to controller helper `FUN_18001ac10`. Fresh REA pseudocode shows that helper creates or selects a controller-side “user preset” and invokes the controller change/refresh callbacks; it is preset management, not a per-sample DSP control. The reconstruction currently exposes the 15 DSP controls above and does not reproduce that preset-selection facility. Evidence for the helper is `ev_dc0d9bf8e3d62d4ea2c0be1015c352cf586feb9eaa9498620ce83e22442c9019`, saved as [`rea-prst-controller-helper-20261020.json`](evidence/rea-prst-controller-helper-20261020.json). The normalized-to-plain setter evaluates `normalized^exponent * (maximum - minimum) + minimum`; EQ gains use parameter type tag `4`, so their displayed 0.1 dB step does not quantize DSP values. The result is capped at the configured maximum. Direct calls to the original setter match the port at all 321 normalized gain points. For high shelf, direct calls through the original parameter handler now match the port's builder arguments, custom power outputs, and all five coefficients at 321 gain points for each of 44.1, 48, and 96 kHz (1,926 section-builder calls). The isolated +9.9 dB mismatch was a summation-tree error in the custom power helper, fixed to add its second and third polynomial terms before adding the first. Six one-ULP differences remain in a separate 10,001-point arbitrary base-10 helper sweep, outside the tested active shelf inputs. REA shows the host callback consumes the last point in each VST3 parameter queue and forwards its normalized value unchanged; the sample offset is unused, matching `VstProcessor::process`. Evidence: setter `ev_d3fcd28f8701359af704a2d6c6f705f8f74fd7e727951d13d37a9e86c0745283`; callback `ev_65363331d2fbc82de1e68f9ca4a9302d109f92a4b0f65b367f91e510002e3182`; fanout `ev_cb86db512fbb2f62845ff0d80e22ed1929e3f097c4e91758404bd2915aea156f`; power helper `ev_97528b19a6d7e2a27c5615dbc67b921dd9d31ff3297f76647710d830df11e64e`; handler results `evidence/high-shelf-handler-multirate-parity-20261020.json`.

Output converts normalized host value `n` to plain dB as `−24 + 40 * n^p`, where `p = 0x1.6437c5692b3ccp−1` (about 0.695738). The inverse conversion raises `(dB + 24)/40` to `1/p`. Thus plain 0 dB is normalized about `0.479879022`, and the shipped default remains 0 dB. Licensed runtime renders at normalized `0.479879022`, `0.6`, and `0.8012644258244325` matched the native port bit-for-bit in the tested bypass setup; the latter values exercise the higher portion of the curve. This supersedes earlier notes that treated the first value as inconsistent with the constructor.

A fresh REA read of Output formatter `FUN_180008a70` confirms it applies the stored exponent and range, then formats either an enum label or a numeric string at the configured precision. The registration constructor `FUN_18000c120` passes plain `0` as Output's initial value and overwrites its exponent with `0x3fe6437c5692b3cc`; combined with the recorded `−24..+16 dB` range, normalized `0.479879022` maps to approximately `0 dB`. This resolves the earlier REAPER formatting discrepancy; a serialized project value alone still cannot establish whether a host instance is fresh. Full REA records: [`rea-output-formatter-20261020.json`](evidence/rea-output-formatter-20261020.json), Evidence `ev_30564e14f9b14eb8144032022afe3d232b05acc2fed1a73633b26532b8e15f19`; [`rea-register-controls-20261020.json`](evidence/rea-register-controls-20261020.json), Evidence `ev_7c184e4755d03161a77353784cf11e09e2b1dd053ef28941ca960579024fba69`.

## Parent processing path

For each sample on the ordinary stereo path, the parent:

1. Updates 10 Hz smoothers for input model gain, Output gain, model post factor, and polarity.
2. Applies the model input factor and processes each channel through its selected model. In mono, the model runs once and its result is duplicated after the EQ path.
3. Applies an additional one-pole only to the first stream. Its constructor coefficient is `1 − exp(−2π) + 0.001867`; the smoother memory starts at zero and survives sample-rate reset.
4. Runs each stream through its independent EQ state.
5. Multiplies by Output gain, the model post factor, the fixed unity factor, and smoothed polarity, in that order.

The ordinary-path offset is zero and the fixed factor is one. The parent sample callback's current port matches the observed ordering and mono behavior (fresh REA Evidence `ev_866258d4f4631a2a3ca4efbb962ad0e8888071d722a78fff335b23cca6ed7949`, `rea-redo-parent-process.json`). A new full REA read of `FUN_18000f6a0` confirms that the `+0xbd0` branch copies each channel and returns before advancing DSP state. On the ordinary branch it increments a block counter and checks rate-normalized thresholds before potentially changing the input offset or fixed output factor when stored configuration values differ. The constructor initializes the guard from a shipped zero byte and no in-module setter was found; the mutation path has not been observed at runtime. The port therefore matches the documented ordinary path but does not model that exceptional delayed mutation path. Fresh pseudocode and assembly are retained in [`rea-parent-process-refresh-20261020.json`](evidence/rea-parent-process-refresh-20261020.json), REA Evidence `ev_9c1932caf864f37056213219ed732deddded5bdc5bf8fd0f1728ef8e73cff938`. The VST3 host bypass queue is handled separately from this model-selector bypass.

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

The shared high-shelf builder computes `A = 10^(gainDb/40)`, `w = frequency * 2π / sampleRate`, and `alpha = sqrt(A) * 2 * (sin(w)/(shape*2))`, then writes the five biquad coefficients in the observed order. REA confirms positive finite `A` uses hardware `SQRTSD` and the scalar coefficient expressions match the source order. The older builder-only parity report predates the nested section-A lookup correction and does not cover the corrected active inputs; it is superseded by the direct parameter-handler comparison. That comparison now reports exact builder arguments, power outputs, and coefficient bits across 321 gains at 44.1, 48, and 96 kHz. The final plugin's sample-level output still needs an end-to-end render sweep. Evidence: builder `ev_55a417ed638c00d0f677507de37160ae40297f3b7260b7cbcdfa76116c4d7889`; sqrt `ev_90f4999489c969e79bb636aa1f6e8da6a5fc97c873288165925f588442a76f49`; handler comparison `evidence/high-shelf-handler-multirate-parity-20261020.json`. Low shelf follows its corresponding numerator/denominator equations. EQ In off installs the dedicated 6 Hz / Q .99 high-pass and rebuilds the shelf/mid gains at zero while preserving selected frequency indices.

### Mid section

Mid gain first passes through a sign-specific nonlinear transform and table lookup. The six frequency indices select different center-frequency and bandwidth factors; positive gain applies an additional per-index scale. `buildMid` derives its coefficients from the transformed gain, bandwidth, center frequency, tangent terms, and the target's square-root expressions. A near-zero gain installs identity. The mid section's recurrence adds `a1*y1 + a2*y2`, unlike the other EQ sections; this was required to prevent instability and match the shipped processor.

### EQ runtime coverage

At 44.1 kHz, licensed-original versus rebuilt Win64 VST3 sweeps established bit-identical low-shelf responses at all four frequency selections for gains −16, −9.6, 0, +9.6, and +16 dB. A dense 0.1 dB low-shelf sweep had 1,281/1,284 bit-identical pairs; the other three residuals were below −229 dB. A dense mid sweep over all six center frequencies had 1,886/1,926 bit-identical pairs; the remaining residuals were below −281 dB. High-pass tests covered the four active choices and matched at approximately −159.9 dB residual on the captured tone. A 20 Hz–20 kHz log sweep matched selected shelf settings, including both signs at low shelf and high shelf.

The earlier post-lookup 44.1 kHz end-to-end render sweep produced bit-identical audio at the five former outliers (+1.4, +1.5, +1.6, +1.8, and +2.3 dB). It measured 203/321 bit-identical gain settings, with 118 residual settings between −232.24 and −229.79 dB and maximum peak error below `8.84e−13`. Those audio captures predate the latest custom-power polynomial correction and are not evidence of the current build’s render parity. The original-handler coefficient comparison is now exact at every setting and three sample rates; rerun the audio sweep with the current build. Earlier measurement record: `high-shelf-dense-gain-postfix-20261020.json`.

Two tangent-core details were required for 1:1 mid coefficients. The target forms `z*x` before multiplying the numerator polynomial; the algebraically equivalent left-associated sequence differed by one ULP. When the secondary reduction rotates an angle outside ±0.68, the target folds the low correction into the angle and clears it before evaluating the polynomial. Retaining that correction produced a further one-ULP error for a 32 kHz, frequency-index 3, +0.6 dB case even though the handler's gain, center-frequency, and bandwidth arguments matched exactly. `binaryTanCore` now preserves both target behaviors. Direct licensed-module comparison matched all 57,780 mid coefficients for 11,556 combinations (321 gain points at each of six frequencies and six rates: 32, 44.1, 48, 88.2, 96, and 192 kHz). Stateful mid-path comparison also matched all 50,291,712 samples after 256-sample warmups and 4,096-sample broadband sequences per combination. The broader mixed-EQ matrix was rerun after both corrections and matched 37,806,080 samples across 355 scenarios with zero mismatches. This validates the listed direct paths and settings; exhaustive parent VST3 host-render parity remains open. REA evidence: mid handler `ev_25f572d64daa429f803b077035a7e36a0baca5f7bc0754a5811a4e04670ef58b`, builder `ev_147fa0d9e694e1f7fff0d0149aa77d986d9fd6499f3e4678a2557c15dcb6668f`, and tangent core `ev_71d321d7cb8dd5e814bb53da6a0be8c5e86df97c31b014ea549672f67c246d41`; saved as `rea-mid-handler-20261020.json`, `rea-mid-builder-20261020.json`, and `rea-tan-core-20261020.json`. Runtime matrices are in `mid-eq-stateful-sample-parity-20261020.json`.

A dense low-shelf sweep exposed one separate section-B `b2` ULP difference at 32 kHz, frequency index 0, −14.3 dB. REA identified a one-ULP difference in the sine helper, while power and cosine outputs matched. In the small-angle sine polynomial, the target forms `x6 = x2*x2*x2`, multiplies the high polynomial by that product, subtracts `1/6` from the low polynomial, then adds high and low parts. `binarySinCos` now preserves that grouping. Direct comparisons then matched 77,040 low-shelf coefficients across 7,704 settings and six rates, plus 33,527,808 stateful samples across the same gain/frequency/rate grid. Dense high-shelf comparison also matched 19,260 coefficients across 1,926 settings at those rates after the shared sine change. The mixed-EQ sample matrix was rerun and remained bit-identical (37,806,080 samples). REA evidence: low handler `ev_b0fca849c2a3c158115b8258675343477b55e255da35e8fc5a8ab325fad14ad4`, section-B builder `ev_ea16ee85c9067f338477045b560d80cb7439a0dd1d9c02cc116628aa513c739c`, and sine helper `ev_3c33fe851ddff95178ed38737fd3ec2e34d0a06b0fa77c9e49141f01338e4099`; runtime reports are `low-shelf-dense-parity-20261020.json` and `high-shelf-dense-coefficient-parity-20261020.json`. These are direct coefficient and stateful EQ-path comparisons; full VST3 host-render parity remains open.

Fresh REA pseudocode for `FUN_180011e40` confirms the section-A frequency interpolation uses the intermediate coordinate as the next lookup input (Evidence `ev_7fc6108952a4e5517dfa2ac342f4b3e7bdd679451d755f76dc9586d582fced94`). A fresh read of dispatcher `FUN_18000fc80` confirms indices 5 and 11 trigger high-shelf recalculation for the two channel states and pass values loaded from the selected parameter objects’ `+8` field (Evidence `ev_2d72a2d07d2ee37a571d4306f5752a814fe72bedb982215dfc19aa6a35c89d77`). The VST3 callback uses the final normalized queue point; its exact host-supplied bits remain unobserved. However, direct calls through the original high-shelf handler now match all builder arguments and coefficient tuples at the tested 321 settings for 44.1, 48, and 96 kHz. End-to-end audio parity for the current build remains open. Main target overview Evidence is `ev_19eeb42f4e4b250a13682b092df341be1197f5ae1760fbca37e76543ae2c46a` (`rea-current-overview-20261020.json`), matching the shipped VST3 SHA-256 recorded above.

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

The asymmetric Mic waveshaper uses separate positive/negative cubic and fifth-order coefficients. For `z`, it forms `z3=(z*z)*z` and `w=((z3*c3)*m)+z+((((z3*z)*z)*c5)*m)`, where `m=max(magnitude,0)`. It clamps `w` at the same limits as Line, then applies the bounded rational shaper. The new shaper output updates drive subtraction, low-frequency output, two high-pass feedback paths, a fixed-mix smoother, and the final output smoother. The routine's state and arithmetic order match fresh REA Evidence `ev_88fdb9d22c10f9ee2d24e047b11df352519bf4ea53361a2cd830aab4a58170b8` (`rea-redo-mic-process.json`). A direct call to the licensed helper `FUN_180015ce0` matched `shapeMic` bit-for-bit across 1,400,007 signed-input/drive combinations from −5 to +5; the `m=0` cases also exercise the shared clamp/rational path used by Line shaping. This isolates shaper arithmetic and does not cover coefficient setup or stateful Mic audio. Result: `evidence/mic-shaper-helper-bit-parity-20261020.json`; helper REA Evidence `ev_fdc39f963e7b184b598a2e03384bec53515f10a988f84d7e1bdaa7ce332b039b`.

Fresh REA reads of Mic reset `FUN_180015da0` and mode setup `FUN_1800163f0` clarify the second biquad's lifecycle. Reset calls a general coefficient routine on the block at `+0x4d0`; Mic mode setup writes the active coefficients into that same block (`+0x508` through `+0x528`) from `1413.7166941154069 / fs` and `10^-0.075`. Those writes map to `filterB` in `configureMic`; the reset and setup write order explains why the mode-specific coefficients are active. Reset also stores envelope alphas at `+0x2c8` and `+0x308`, which the sample routine reads directly. The current port matches these reset and mode-setup writes. Complete records: [`rea-mic-reset-20261020.json`](evidence/rea-mic-reset-20261020.json), Evidence `ev_7ffd4405faf0c2faa07be78942699ae88bc6b3b9b62afbfcf683779d48636e2e`; [`rea-mic-mode-setup-20261020.json`](evidence/rea-mic-mode-setup-20261020.json), Evidence `ev_999202be06f037d655b0a0b868b26d2fd19252408dd28c4f94fef6d2005f8346`.

The target's Windows CRT random generator uses `holdrand = holdrand*0x343fd + 0x269ec3` and returns `(holdrand >> 16) & 0x7fff`. Parent construction time-seeds the current thread and consumes thousands of draws before Mic setup. The exact host thread and initial timestamp affect reset state. Controlled steady-tone comparisons found seed changes small for those captures, but matched random startup across hosts and deterministic transient parity are not fully established.

### Bypass model

Selector 8 still processes audio. It applies `input + 0.03*(input-low)` and a dedicated high-shelf section whose coefficient path differs from the shared EQ builder. Its fresh sample routine was previously captured as Evidence `ev_5159fe699666a03008b5576202055b509a883ff6c717cb1fb22e6882ead47896`; reset Evidence is `ev_a6279251d68197edae959bfca741bb21494e5c1fe7f29a79772bca32d00eec4a`. A separate block-bypass request copies audio and freezes DSP state.

## Measured parity and open work

Runtime evidence uses licensed-original and reconstructed VST3 renders in REAPER or a local Wine VST3 harness, with channel alignment and paired parameter values recorded in `PROGRESS.md`. No REAPER MCP was used. Direct child-sample comparisons now match the licensed Line processor bit-for-bit for 3,932,160 samples across all eight Line modes and five sample rates, and the Mic processor bit-for-bit for 5,898,240 samples across selectors 9–20 and five rates when its reset random draw is aligned. Earlier VST3-hosted Mic tone captures at 44.1 kHz showed close deterministic output; Mic 60 residuals were concentrated in near-Nyquist folded harmonics. The four-second Mic 60 impulse tail varied between original runs, so host-level transient parity still needs matched constructor/reset random history.

Current unresolved work:

1. Rerun end-to-end high-shelf audio parity with the current Win64 build. Direct calls through the original handler now match all tested builder arguments and coefficient bits at 321 gains and three sample rates. The earlier 321-point render has 118 tiny residual settings and predates the final custom-power fix, so it does not establish current-build output parity. The latest local probe rendered the current port with the intended 1.4 dB high-shelf settings, but the licensed original hit Wine `EXCEPTION_BREAKPOINT` at `ntdll+0x59644` before writing audio, even after reimporting and verifying the license registry value. The probe source is not retained, and this run does not compare target and port outputs. REAPER also failed to produce a fresh render in this environment. Details are in [`host-probe-current-build-20261020.json`](evidence/host-probe-current-build-20261020.json).
2. Expand EQ settings and sample-rate coverage, particularly automation and state transitions where retained smoother/filter histories matter.
3. Establish deterministic Mic reset/random thread state across the VST3 host boundary, then compare transient and broadband response over time.
4. Re-run end-to-end checks on the final rebuilt binaries after any parity fix. Static REA agreement and selected passing renders do not establish complete 1:1 behavior.

## REA evidence for the restored core analysis

The following REA/Ghidra records were captured against the supplied original plug-in while rebuilding this document. Historical full payloads were removed during the requested history cleanup; their Evidence IDs remain cited in the topic sections. The fresh formatter read below is retained as a full JSON record.

| Target routine | Evidence | Earlier record name |
|---|---|---|
| Parent stereo/mono sample callback `FUN_18000f6a0` | `ev_866258d4f4631a2a3ca4efbb962ad0e8888071d722a78fff335b23cca6ed7949` | `rea-redo-parent-process.json` |
| EQ cascade `FUN_180013090` | `ev_a9c2ba22e8269529fef23b68425e8d7d743a0033af6bc4e85fcc62f6922c18e3` | `rea-redo-eq-cascade.json` |
| High-shelf handler `FUN_180011e40` | `ev_2109e4c402969e14400ea04351dbf92a0342bb7ccae53262d8c467cc0428108c` | `rea-current-highshelf-handler-followup.json` |
| Shared biquad builder `FUN_1800112b0` | `ev_58bdc7f646b07f919901fb1c37890f946f08f7a85ff205005d8110ff5ef52413` | `rea-redo-biquad-builder.json` |
| Line sample routine `FUN_1800152c0` | `ev_4749950d134de09866b6e6bb671e7753a947ab651ff6c6008bbae04e71086249` | `rea-redo-line-process.json` |
| Line sample-rate reset `FUN_180014860` | `ev_44b51fa7a1119068226421ca5be222586a47655ed80b587cd150daa933b593cc` | `rea-line-reset-20261020.json` |
| Mic sample routine `FUN_180016a90` | `ev_88fdb9d22c10f9ee2d24e047b11df352519bf4ea53361a2cd830aab4a58170b8` | `rea-redo-mic-process.json` |
| Output formatter `FUN_180008a70` | `ev_30564e14f9b14eb8144032022afe3d232b05acc2fed1a73633b26532b8e15f19` | `rea-output-formatter-20261020.json` |
| Control registration `FUN_18000c120` | `ev_7c184e4755d03161a77353784cf11e09e2b1dd053ef28941ca960579024fba69` | `rea-register-controls-20261020.json` |

Earlier supporting Evidence IDs for parameter conversion, reset/setup, nonlinear helpers, low-shelf/mid handlers, the bypass path, and runtime argument captures are indexed chronologically in `PROGRESS.md`; their full JSON payloads are no longer tracked.

## Latest direct high-shelf handler comparison

A statically linked MinGW harness loaded the licensed original VST3 under Wine and called `FUN_180011e40` across 321 requested gains from −16 to +16 dB in 0.1 dB increments at 44.1 kHz, then repeated the sweep at 48 and 96 kHz. All 1,926 builder calls matched the port on gain, integer frequency, shape, and sample rate. All active custom-power outputs and every coefficient bit matched at each rate. A breakpoint capture localized the former +9.9 dB discrepancy to the polynomial summation tree in `FUN_1800ee000`: the target adds the second and third terms, then adds the first. `binaryPower` now follows that order. Evidence: `evidence/high-shelf-handler-bit-parity-20261020.json`, `evidence/high-shelf-handler-multirate-parity-20261020.json`, and REA helper record `evidence/rea-power-helper-active-mismatch-20261020.json`. The current static MinGW Win64 artifact is SHA-256 `e7da601840aba61e184c77613711ffc2798d34ec147605f58a7652c54fb4b2e4`. A local probe run completed this port build with model selector OFF, high shelf +1.4 dB, High Shelv Power and EQ In on, and Output 0 dB; a matching licensed-original run still stopped at Wine `ntdll+0x59644` without output after the license value was reimported and checked. The full run record is `evidence/host-probe-current-build-20261020.json`. No current-build target-versus-port render comparison is available.

## Deterministic Line stateful sample parity

A controlled Win64 comparison called the original Line constructor (`FUN_180014320`), sample-rate reset (`FUN_180014860`), mode setup (`FUN_180014e40`), and in-place sample routine (`FUN_1800152c0`) directly from the licensed VST3 under Wine. It applied the same lifecycle to the port by resetting `ModelBank` at the same rate and mode, then compared the output sample bits after each frame. The explicit sample-rate reset matters: the constructor initially sets a `0.99` final-smoother coefficient, while reset replaces it with the rate-dependent coefficient from `FUN_180014860`.

Across sample rates 44.1, 48, 88.2, 96, and 192 kHz; Line modes 0–7; and multi-impulse, dual 997 Hz/16 kHz tone, and broadband-noise inputs, all 3,932,160 compared samples matched bit-for-bit. The complete result is [`line-stateful-sample-parity-20261020.json`](evidence/line-stateful-sample-parity-20261020.json). Fresh reset evidence is [`rea-line-reset-20261020.json`](evidence/rea-line-reset-20261020.json), Evidence `ev_44b51fa7a1119068226421ca5be222586a47655ed80b587cd150daa933b593cc`. A fresh reference query for the sample routine returned `ev_64939b7e7a482f6ec69e3225a426f9f07f750d932c7255dc0e1d1a2c7b71edfa`; its raw references are retained in [`rea-line-sample-xrefs-20261020.json`](evidence/rea-line-sample-xrefs-20261020.json).

This verifies the direct modeled Line sample path for the tested lifecycle, modes, rates, and stimuli. It does not exercise VST3 host audio/parameter plumbing or parent output-factor smoothing.

## Deterministic Mic stateful sample parity

A controlled Win64 comparison called the original Mic reset (`FUN_180015da0`), Mic mode setup (`FUN_1800163f0`), and sample routine (`FUN_180016a90`) directly from the licensed VST3 under Wine, then compared each returned sample with `ModelBank::processMic`. The harness seeded the target thread-local Windows CRT generator to a fixed value before each case, captured the reset draw, and initialized the model with the same draw. It also checked that target and model random states and the Mic coefficient/factor tuple agreed. This avoids host startup's unrelated time-seeded random history and provides a deterministic sample-path comparison.

Across sample rates 44.1, 48, 88.2, 96, and 192 kHz; selectors 9–20; and multi-impulse, dual 997 Hz/16 kHz tone, and broadband-noise inputs, all 5,898,240 compared samples matched bit-for-bit. No factor-tuple mismatch occurred. The first run localized the initial state divergence to the DC smoother: the port used decimal `0.00161107219`, while REA pseudocode gives `0.0016110721900001635`. Replacing the rounded literal with its exact binary64 hexadecimal literal fixed the complete matrix. Evidence and the reproducible scope summary are in [`mic-stateful-sample-parity-20261020.json`](evidence/mic-stateful-sample-parity-20261020.json).

This proves the modeled Mic sample path for the tested reset state, modes, rates, and stimuli. It does not prove the VST3 host's constructor-time random history or complete plugin-level parity, and it does not close the remaining high-shelf audio and EQ transition checks.
