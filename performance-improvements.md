# KI1H — Code Review: Performance & Cleanliness

**Scope:** all 9 files in `src/` (~2,470 lines), 7 modules.
**Target:** VCV Rack 2, built against `../Rack-SDK` with `-std=c++11 -O3 -funsafe-math-optimizations`.

Everything below cites `file:line` against the tree as of commit `09404ad`.

---

## Summary

The plugin is structurally sound — one file per module, a consistent `Module` / `Widget` /
`Model` layout, and a shared layout grid in `plugin.hpp` that keeps panel code readable. The
problems are concentrated in three places:

1. **Six correctness defects** sitting in per-sample code paths. One is a stack buffer
   overflow that runs 44,100+ times a second; two are shared mutable state across module
   instances and engine threads; two are uninitialized filter state; one makes the FILTER
   module start up silent.
2. **Per-sample transcendental math** — `std::pow`, `std::exp`, `std::sin`, `std::cos` —
   called where the Rack SDK already ships cheap approximations, and recomputed every sample
   from values that only change when a knob moves.
3. **Substantial duplication** — `softLimit` and `struct Channel` are copy-pasted between two
   modules, the waveform generators are duplicated between VCO and LFO, and
   `KI1H_ENVELOPE::process` is the same 26 lines written out four times.

### Severity-ranked top items

| # | Severity | Item | Location |
|---|----------|------|----------|
| 1 | **Critical** | `sizeof()` used as an element count → 20-iteration loop writes past two stack arrays | `KI1H_MIX.cpp:96` |
| 2 | **High** | Global `SchmittTrigger` shared by every VCO instance and raced across threads | `KI1H_VCO.cpp:6` |
| 3 | **High** | `static` RNG in a member function — shared across instances, data race across threads | `KI1H-KAOS.cpp:73-79` |
| 4 | **High** | Uninitialized filter state read before first write | `KI1H_FILTER.cpp:25,50` |
| 5 | **High** | `configParam` defaults below their own minimum → FILTER starts silent | `KI1H_FILTER.cpp:184-215` |
| 6 | **Medium** | 9-harmonic Fourier loop with `pow`+`sin` per sample | `KI1H_VCO.cpp:241-265` |
| 7 | **Medium** | `pow`/`exp`/`cos`/`sin` per sample where SDK approximations exist | VCO, LFO, FILTER |
| 8 | **Medium** | `KI1H_ENVELOPE::process` is 105 lines of 4× copy-paste | `KI1H_ENVELOPE.cpp:271-376` |

---

## 1. Correctness defects

These were found while auditing the hot paths for performance. They're listed first because
optimizing broken code is the wrong order of work.

### 1.1 `sizeof()` used as an element count — stack buffer overflow

`src/KI1H_MIX.cpp:93-105`

```cpp
void Mix::process(std::array<float, 5> all) {
  std::array<float, 2> evens;
  std::array<float, 3> odds;
  for (unsigned long i = 0; i < sizeof(all); i++) {   // <-- sizeof, not size()
    if (i % 2 == 0)
      odds[i / 2] = all[i];
    else
      evens[i / 2] = all[i];
  }
```

`sizeof(std::array<float,5>)` is **20** (5 floats × 4 bytes), not 5. The loop runs 20 times:

- reads `all[0..19]` — 15 elements past the end of a 5-element array;
- writes `odds[0..9]` into a 3-element array;
- writes `evens[0..9]` into a 2-element array.

This happens on **every sample**, so it's a continuous out-of-bounds read and write into the
audio-thread stack frame. That it hasn't visibly crashed is luck about frame layout, not
safety. It also means `allOut`/`leftOut`/`rightOut` are computed from garbage in the tail
elements.

**Fix:** `for (std::size_t i = 0; i < all.size(); i++)`.

This one is worth a regression test — `Mix::process` is a pure function of its input and is
trivially testable off the audio thread.

### 1.2 File-scope `SchmittTrigger` shared by all VCO instances

`src/KI1H_VCO.cpp:6`

```cpp
dsp::SchmittTrigger syncTrigger;
```

Used at `KI1H_VCO.cpp:202` inside `ShaperOscillator::process`. Two consequences:

- **Shared state:** every KI1H-VCO in the patch hard-syncs off one trigger object. Two VCOs
  with different sync sources will steal each other's edges.
- **Data race:** Rack's engine runs modules across worker threads. `SchmittTrigger::process`
  reads and writes `state`, so concurrent VCO instances race on it. Undefined behavior, and
  in practice intermittent missed or doubled syncs.

**Fix:** make it a member of `ShaperOscillator` (alongside the `phase`/`output` state it
already owns). `KI1H-KAOS.cpp:19-20` already does this correctly with its two triggers.

### 1.3 `static` RNG inside a member function

`src/KI1H-KAOS.cpp:73-79`

```cpp
float KAOS::generateNoise(float seed) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::normal_distribution<float> dis(0.0f, 1.0f);
  return dis(gen) * 1.5f;
}
```

Function-local statics are one object for the whole process. `mt19937::operator()` advances
the generator state, and `std::normal_distribution` caches the second Box–Muller variate
between calls — **both** are mutating. Multiple KAOS instances on different engine threads
race on both. Same class of bug as 1.2.

The `seed` parameter is also unused (the call site at line 36 passes `noise`, the previous
output — presumably an intent that was never wired up). `-Wno-unused-parameter` is in the
SDK's flags, so this never warned.

**Fix:** Rack ships a thread-local generator — `rack::random::normal()` (`random.hpp:120`)
returns a standard normal and is safe here. Drop the `seed` parameter, or make the intent
explicit if the feedback was deliberate.

### 1.4 Uninitialized filter state

`src/KI1H_FILTER.cpp:25` and `:50`

```cpp
struct LPFilter : Filter {
  float stages[12];              // no initializer
  float cutoff_coeff;
};

struct BPFilter : Filter {
  float x1, x2, y1, y2;          // no initializer
  float b0, b1, b2, a1, a2;
};
```

Rack heap-allocates modules, so these members start indeterminate. They are then **read
before ever being written**:

- `LPFilter::process:125` — `float feedback = stages[11] * resonance;` on the first sample.
- `BPFilter::process:168` — `output = b0*hp_out + b1*x1 + b2*x2 - a1*y1 - a2*y2;` reads
  `x1/x2/y1/y2` before the assignments on lines 170-173.

Both are inside feedback loops, so a garbage value large enough to be an Inf or NaN never
decays out — the filter is dead until the module is deleted. This is compounded by
`-funsafe-math-optimizations` (from the SDK's `compile.mk:21`), which makes NaN/Inf behavior
undefined and lets the compiler assume they can't occur.

**Fix:** brace-initialize (`float stages[12] = {};`, `float x1 = 0.f, x2 = 0.f, ...`) and add
an `onReset()` override that clears filter state — Rack calls it on module init and on
user-requested reset.

The coefficients `b0..a2` happen to be safe because `setCoefficients` (line 163) runs before
they're read, but initializing them costs nothing and removes the reasoning burden.

### 1.5 `configParam` defaults outside their own range

`src/KI1H_FILTER.cpp:184-215`

| Param | Line | Range | Default |
|-------|------|-------|---------|
| `LPFreq` | 184 | 20 – 22000 | **0.1** |
| `BPFreq1` | 194 | 30 – 15000 | **0.1** |
| `BPFreq2` | 203 | 30 – 15000 | **0.1** |
| `BPWidth1` | 196 | 0.5 – 5 | **0** |
| `BPWidth2` | 205 | 0.5 – 5 | **0** |
| `BPRes1` | 197 | 0.01 – 1.666 | **0** |
| `BPRes2` | 206 | 0.01 – 1.666 | **0** |

`Module::configParam` assigns the default straight through without clamping — verified in
`Rack-SDK/include/engine/Module.hpp:143-144`:

```cpp
Param* p = &params[paramId];
p->value = q->getDefaultValue();
```

So a freshly-placed KI1H-FILTER has a **0.1 Hz** low-pass cutoff. At 48 kHz that gives a
per-stage coefficient of `1 - exp(-2π·0.1/48000) ≈ 1.3e-5`, through 12 cascaded poles —
the module is effectively silent until the user moves the knob. The `HPFreq` default of `1.f`
(line 215) is also below its 30 Hz minimum.

**Fix:** pick in-range defaults (e.g. `LPFreq` 1000, `BPFreq` 1000, `BPWidth` 1.0,
`BPRes` 0.01, `HPFreq` 30).

### 1.6 Output array indexed with an input enum

`src/KI1H_MIX.cpp:150`

```cpp
if (outputs[CV1 + i].isConnected())
```

`CV1` is from `INPUT_IDS`, used to index `outputs`. It only does the right thing because
`CV1 == 0` and `OUT1 == 0` happen to coincide. Reordering either enum silently breaks the
mix bus. The equivalent line in VCA (`KI1H_VCA.cpp:220`) correctly uses `outputs[OUT1 + i]`.

**Fix:** `outputs[OUT1 + i]`.

---

## 2. Performance

Everything in this section runs **once per sample per module instance** — 44,100 to 192,000
times a second, on the audio thread, where a missed deadline is an audible dropout.

### 2.1 `std::pow(2.f, pitch)` for pitch → frequency

| Location | Calls/sample |
|----------|--------------|
| `KI1H_VCO.cpp:115` (`Oscillator::calculateFreq`, called by both oscillators) | 2 |
| `KI1H_LFO.cpp:86` (`LFO::process`, called for `lfo1` and `lfo2`) | 2 |
| `KI1H_LFO.cpp:121,123` (`SampleAndHold::process`) | 2 |

`std::pow` with a runtime exponent is a full generic power — typically 50-100+ cycles. The
SDK ships a purpose-built replacement in `dsp/approx.hpp:118`:

```cpp
#include <dsp/approx.hpp>
return dsp::FREQ_C4 * dsp::exp2_taylor5(pitch);
```

`exp2_taylor5` is a 5th-order polynomial with exponent-field manipulation — accurate to well
under a cent over the audio range, and roughly an order of magnitude cheaper. The LFO alone
pays this cost 4× per sample.

### 2.2 `generateShapedWave` — the most expensive routine in the plugin

`src/KI1H_VCO.cpp:241-265`

```cpp
int maxHarmonics = (int)(8.f * (1.f - harmonicReduction)) + 1;
for (int h = 1; h <= maxHarmonics; h++) {
  float amplitude = 1.f / h;
  float harmonicGain = std::pow(1.f - harmonicReduction, h - 1);
  result += amplitude * harmonicGain * std::sin(2.f * M_PI * ph * h);
}
```

Up to 9 iterations, each with a `std::pow` **and** a `std::sin`, per sample. That is up to 18
transcendental calls per sample for one oscillator. Two observations:

- `harmonicReduction` derives from `shape`, a knob (plus CV) — it changes at control rate, not
  audio rate. `amplitude * harmonicGain` for each harmonic can be computed once when `shape`
  changes and cached in a small array, removing all the `std::pow` calls.
- The `std::sin` calls remain the dominant cost. A recurrence (Chebyshev / phasor rotation) or
  a wavetable is the standard fix, or replace the whole thing with a closed-form saw↔sine
  morph — e.g. `sin(2π·ph)` blended toward the naive saw, or a `tanh`-style waveshaper applied
  to a saw core. Any of these is a single-digit-cycle operation.

Note this method also has no anti-aliasing benefit to offset the cost: it band-limits to
≤9 harmonics regardless of pitch, so it's simultaneously expensive at low notes and
inaccurate at high ones.

### 2.3 Double-precision transcendentals in the filter

`src/KI1H_FILTER.cpp:122, 141, 161-162` and `:34-36`

```cpp
cutoff_coeff = 1.0f - exp(-2.0f * M_PI * cutoff * sampletime);   // :122
float alpha  = exp(-2.0f * M_PI * cutoff * sampletime);          // :141
float hp_alpha = exp(-2.0f * M_PI * hpFreq * sampletime);        // :161
float cos_w = cos(w);  float sin_w = sin(w);                     // :35-36
```

Two problems compounding:

- `M_PI` is a `double`, so each expression promotes to double, calls the **double** `exp`/
  `cos`/`sin`, then narrows back to float. Unqualified `exp`/`cos`/`sin` resolve to the
  `<math.h>` double overloads.
- These run per sample, per filter. `BPFilter::process` alone does one `exp` + one `cos` +
  one `sin` every sample; with two BP filters plus LP plus HP that's 4 `exp`, 2 `cos`,
  2 `sin` per sample for the module.

**Fix:** `#include <cmath>` and use `std::exp` / `std::cos` / `std::sin` with an `M_PI_F`-style
float constant, which alone removes the double round-trip. Better, see 2.4.

### 2.4 Filter coefficients recomputed unconditionally

`KI1H_FILTER.cpp:122` and `:163` recompute coefficients on every sample from values that
change only when a knob moves or CV arrives. Two options:

- **Cache-and-compare:** store the last `cutoff`/`q`/`sampletime` and skip `setCoefficients`
  when unchanged. Cheap, but doesn't help when CV *is* connected.
- **Use the SDK primitives** (`dsp/filter.hpp`): `dsp::TBiquadFilter` (line 293) already
  implements the exact RBJ biquad `BPFilter::setCoefficients` is hand-rolling, via
  `setParameters(Type, f, Q, V)`. `dsp::TRCFilter` (line 14) covers the one-pole LP/HP stages.
  Both are maintained, correctly initialized, and have SIMD variants.

A third option worth considering: run coefficient updates on a `dsp::ClockDivider`
(`dsp/digital.hpp:228`) at, say, every 16 samples. Standard practice for control-rate
parameters, and inaudible for filter cutoff.

### 2.5 `std::array` passed by value on the audio path

- `VCA::process(std::array<float,5> channels, std::array<float,5> pans)` — `KI1H_VCA.cpp:105`,
  declared at `:43`. Copies 40 bytes per sample.
- `Mix::process(std::array<float,5> all)` — `KI1H_MIX.cpp:93`, declared at `:41`. Copies
  20 bytes per sample.

Neither function mutates its arguments. **Fix:** take `const std::array<float,5>&`.

### 2.6 `exp()` per sample in the S&H lag

`src/KI1H_LFO.cpp:170-171`

```cpp
float timeConstant = lagTime / 4.605f;
float alpha = 1.0f - exp(-sampleTime / timeConstant);
```

`lagTime` comes from the `SLAG_PARAM` knob (`KI1H_LFO.cpp:283`) — control rate. The `exp` is
recomputed every sample to produce a value that almost never changes.

**Fix:** cache `alpha`, recomputing only when `lagTime` or the sample rate changes (Rack calls
`onSampleRateChange()` for the latter). Or use `dsp::TExponentialFilter`
(`dsp/filter.hpp:59`), which is exactly this one-pole smoother with the coefficient handled
for you.

### 2.7 The S&H recomputes an oscillator the LFO already has

`src/KI1H_LFO.cpp:118-133` runs two full phase accumulators, each with its own
`std::pow(2.f, ...)`. But the S&H is driven by `pitch2` (`KI1H_LFO.cpp:304`) — the same pitch
already used for `lfo2` at line 275. The `phase` accumulator inside `SampleAndHold` duplicates
`lfo2.phase` sample for sample.

**Fix:** pass `lfo2`'s phase (or its frequency) into `SampleAndHold::process` rather than
recomputing it. Saves one `pow` and one accumulator per sample. The clock phase is genuinely
independent and must stay.

### 2.8 No short-circuiting on disconnected outputs

None of the modules check whether their outputs are patched before doing the work:

- `KI1H_VCO::process` (`:321-402`) always renders both oscillators plus the sub, even with
  nothing connected.
- `KI1H_FILTER::process` (`:236-315`) always runs all four filters.
- `KI1H_ENVELOPE::process` (`:271-376`) always runs all four envelopes and writes 12 outputs.
- `KI1H_LFO::process` always runs both LFOs and the S&H.

Guarding the expensive parts on `outputs[X].isConnected()` is the standard Rack idiom and
costs a predictable branch. Care is needed where an output feeds internal normalling — e.g.
`KI1H_FILTER.cpp:302` uses `!outputs[BPOUT1].isConnected()` to chain BP1 into the LP, and
`KI1H_VCO.cpp:356` normals osc1's sine into osc2's FM — so those paths must stay live. But
e.g. VCO's sub oscillator (`:150-154`) is used nowhere but `SUB_OUT` and can be skipped
outright.

### 2.9 Per-instance mutable constants

These are non-`const` instance members that should be `static constexpr`:

| Member | Location |
|--------|----------|
| `CV_SCALE`, `PWM_OFFSET` | `KI1H_VCO.cpp:99-100` |
| `CV_SCALE` | `KI1H_LFO.cpp:75`, `KI1H_MIX.cpp:71`, `KI1H_ENVELOPE.cpp:213` |
| `minFreq`, `maxFreq` | `KI1H_FILTER.cpp:23-24, 31-32, 56-57` |

As written each is per-object storage that the optimizer must treat as potentially mutable and
reload. `static constexpr float CV_SCALE = 5.f;` folds them into the instruction stream.

Note `KI1H_ENVELOPE.cpp:203-204` already does this correctly with `minStageTime`/`maxStageTime`
— worth following that pattern consistently.

There's also a knock-on cleanup: `KI1H_FILTER.cpp:184` currently writes
`KI1H_FILTER::lpfilter.minFreq` to reach a non-static member through a class-qualified name,
which compiles only because it's inside a member function. With `static constexpr` it becomes
the straightforward `LPFilter::minFreq`.

### 2.10 Aliasing (noted, not demanded)

None of the oscillators band-limit: `RawOscillator::generateSaw` (`:178`), `generateSquare`
(`:130`), `generateTriangle` (`:171`) and `generateSub` (`:182`) are naive. Hard sync
(`KI1H_VCO.cpp:201-205`) resets phase discontinuously with no correction. This aliases audibly
above roughly the top octave.

This is a deliberate-sounding choice for an analog-modelled beta and fixing it is a
significant piece of work, so it's flagged rather than prescribed. If it's ever on the table,
`dsp/minblep.hpp` ships `dsp::MinBlepGenerator` and Rack's own `VCO` module is the reference
implementation.

---

## 3. DRY & sharing

### 3.1 `softLimit()` duplicated verbatim

`KI1H_MIX.cpp:15-23` and `KI1H_VCA.cpp:16-24` are character-for-character identical:

```cpp
float softLimit(float input) {
  if (fabs(input) > 5.2f) { ... }
}
```

The MIX copy has **external linkage**; the VCA copy is inside an anonymous namespace
(`KI1H_VCA.cpp:15`), which is what currently prevents a link-time collision. That's a fragile
arrangement — the VCA copy is one refactor away from becoming an ODR violation.

### 3.2 `struct Channel` duplicated verbatim

`KI1H_MIX.cpp:28-35` and `KI1H_VCA.cpp:29-36`. Identical definition, identical `process`
implementation (`KI1H_MIX.cpp:85-88` vs `KI1H_VCA.cpp:96-100`, modulo a comment). Same
linkage hazard as 3.1 — the MIX one is at global scope.

### 3.3 Waveform generators duplicated between VCO and LFO

| Function | VCO | LFO |
|----------|-----|-----|
| `generateSine` | `KI1H_VCO.cpp:126-128` | `KI1H_LFO.cpp:180-182` |
| `generateTriangle` | `KI1H_VCO.cpp:171-176` | `KI1H_LFO.cpp:184-189` |
| `generateSaw` | `KI1H_VCO.cpp:178-180` | `KI1H_LFO.cpp:191-193` |
| `generateSquare` | `KI1H_VCO.cpp:130-137` | `KI1H_LFO.cpp:199-201` |

The first three are identical including comments. `generateSquare` differs only in that the
VCO version takes a pulse-width argument — the LFO version is the `pw = 0.5` case.

### 3.4 Phase accumulation repeated 5×

```cpp
phase += freq * sampleTime;
if (phase >= 1.f) phase -= 1.f;
```

`KI1H_VCO.cpp:119-121`, `:150-152` (sub), `KI1H_LFO.cpp:92-94`, `:127-129`, `:131-133`.

Worth noting: the wrap is a single subtraction, so it only handles `phase < 2.f`. At very high
frequencies or very low sample rates the phase can exceed 1.0 by more than 1.0 and the
oscillator silently breaks. A shared phasor with `phase -= std::floor(phase)` (or Rack's
`eucMod`) fixes all five sites at once.

### 3.5 `ADEnvelope` and `ASDEnvelope` are ~90% the same class

`KI1H_ENVELOPE.cpp:28-91` and `:93-161`. Both declare their own `stage`, `envState`,
`attackTime`, `releaseTime`. `evolveEnvelope` (`:65-85` vs `:134-155`) is functionally
identical — the same four-case switch with the cases reordered. `retrigger` (`:36-42` vs
`:101-106`) is identical.

The shared `Envelope` base (`:20-26`) holds only three floats and the `Stage` enum — it's
carrying almost none of the shared behavior it could.

**Suggestion:** move `stage`/`envState`/`attackTime`/`releaseTime`/`retrigger`/`evolveEnvelope`
into `Envelope`, and let the two subclasses differ only in `processTransition`. That's ~60
lines removed and makes the actual behavioral difference (whether there's a sustain stage)
visible in one place.

### 3.6 `KI1H_ENVELOPE::process` is one function written four times

`KI1H_ENVELOPE.cpp:271-376` — 105 lines. Lines 272-320 (AD1 + ASD1) and lines 322-375
(AD2 + ASD2) are the same code with `1`→`3` and `2`→`4` substituted into every identifier:
`atk1Lvl`/`atk3Lvl`, `triggered1`/`triggered3`, `ad1`/`ad2`, `OUT1`/`OUT3`, and so on.

Because the enums are contiguous (`ATK1_PARAM..ATK4_PARAM`, `OUT1..OUT4`, `EOA1..EOA4`), this
collapses cleanly to a loop over `ADEnvelope ad[2]` / `ASDEnvelope asd[2]` with `ATK1_PARAM + i`
style indexing — exactly the pattern `KI1H_VCA::process` and `KI1H_MIX::process` already use.

There's one asymmetry to preserve: line 356 uses `gateTrigger3.isHigh()` for `held2` where the
symmetric line 303 uses `gateTrigger2.isHigh()` for `held1`. Whether that's intentional
(chaining the ADSR2 hold to AD2's gate) or a copy-paste slip should be settled while
refactoring.

### 3.7 Panel screws pasted into all 7 widgets

The same four `createWidget<ScrewBlack>` calls appear in every widget constructor:
`KI1H_VCO.cpp:411-415`, `KI1H_LFO.cpp:320-324`, `KI1H_MIX.cpp:171-175`,
`KI1H_FILTER.cpp:324-328`, `KI1H_ENVELOPE.cpp:385-389`, `KI1H_VCA.cpp:244-248`. (KAOS at
`KI1H-KAOS.cpp:156-157` uses only two, being a narrow panel.)

**Suggestion:** an inline `addPanelScrews(ModuleWidget* w)` helper in `plugin.hpp`, next to the
existing layout constants — 24 lines become 6.

### 3.8 Filter CV-mod handling repeated 5×

`KI1H_FILTER.cpp:262-291`:

```cpp
if (inputs[XMOD_IN].isConnected()) {
  xFreq += xMod * 1000.f;
  xFreq = clamp(xFreq, x.minFreq, x.maxFreq);
}
```

Five near-identical blocks (LP, BP1, BP2, HP, BigKnob), plus two more for the width inputs at
`:272-273` and `:280-281`. A small `applyFreqMod(Input&, float base, float min, float max)`
helper covers all of them.

### 3.9 VCA pan-CV branches duplicated

`KI1H_VCA.cpp:179-202`. The `i == 0` block and the `i == 4` block are identical apart from
reading `PAN_CV1` vs `PAN_CV2`, and the `else` branch for channels 2-4 (`:203-209`) repeats the
volume-mode arm a third time.

**Suggestion:** select the mode first, then apply once:

```cpp
int mode = (i == 0) ? (int)params[PAN_CV1].getValue()
         : (i == 4) ? (int)params[PAN_CV2].getValue()
         : 0;
if (inputs[CV1 + i].isConnected()) { /* one copy of each arm */ }
```

24 lines become ~8.

### 3.10 Proposed shared header

Everything above points at one missing file. Suggested `src/dsp.hpp`:

```cpp
#pragma once
#include "plugin.hpp"

namespace ki1h {

// §2.9 — shared voltage scaling
static constexpr float CV_SCALE_5V  = 5.f;
static constexpr float CV_SCALE_10V = 10.f;

// §3.1
inline float softLimit(float x);

// §3.4 — one wrap implementation, handles phase > 2.0
struct Phasor {
  float phase = 0.f;
  void advance(float freq, float dt);
  void reset() { phase = 0.f; }
};

// §3.3 — one copy of each waveform
inline float sine(float ph);
inline float triangle(float ph);
inline float saw(float ph);
inline float ramp(float ph);
inline float square(float ph, float pw = 0.5f);

// §3.2
struct Channel { float output = 0.f; void process(float in, float cv); };

// §2.1 — the one place pitch becomes frequency
inline float pitchToFreq(float pitch) {
  return dsp::FREQ_C4 * dsp::exp2_taylor5(pitch);
}

} // namespace ki1h
```

Note this also gives each of §2.1, §3.1-§3.4 a single place to be fixed rather than two to
six. `addPanelScrews` (§3.7) belongs in `plugin.hpp` alongside the existing `ROWS`/`COLUMNS`
constants.

---

## 4. Legibility & consistency

### 4.1 Naming

- **Filename:** `src/KI1H-KAOS.cpp` uses a hyphen; the other six use an underscore
  (`KI1H_VCO.cpp` etc.).
- **Enum names:** `ParamIds`/`InputIds`/`OutputIds`/`LightIds` in VCO (`:64-90`) and LFO
  (`:53-67`) vs `PARAM_IDS`/`INPUT_IDS`/`OUTPUT_IDS` in MIX (`:61-63`), FILTER (`:66-99`),
  ENVELOPE (`:167-197`), VCA (`:59-75`).
- **Enum members:** VCO/LFO use `_PARAM`/`_INPUT` suffixes; FILTER uses bare mixed-case
  (`LPFreq`, `BPMod1`, `BigKnob`) and VCA/MIX use bare uppercase (`PAN1`, `OUT1`, `CV1`).
- **Typo:** `getRightOUt()` — `KI1H_MIX.cpp:48`, `KI1H_VCA.cpp:46` (and both call sites).
- **Typo:** `PCOURSE_PARAM` / `PCOURSE2_PARAM` (`KI1H_VCO.cpp:66,73`) — should be `PCOARSE`.
- **Shadowing:** `KI1H-KAOS.cpp:118` declares `KAOS KAOS;` — a member with the same name as
  its type.
- **`Filter::getOutput()` (`KI1H_FILTER.cpp:15`) is not `const`** while the equivalent getters
  in VCO, LFO, MIX, VCA and KAOS all are.

### 4.2 Declared enums that are never used

`KI1H_VCO.cpp:91` declares `enum Waves { WAVE_TRI, WAVE_SAW, WAVE_SQ, WAVE_PWM };` and then
nothing references it. Both waveform switches use bare integers — `KI1H_VCO.cpp:156-168`
(`case 0/1/2`), `:227-236` (`case 0/1`). LFO has the same pattern with no enum at all
(`KI1H_LFO.cpp:100-112`, `:139-151`).

These `case 0:` labels have to be cross-referenced against the `configSwitch` label strings
(`KI1H_VCO.cpp:280`) to be understood. Using the existing enum makes them self-describing —
and would have caught that `WAVE_PWM` is declared but has no corresponding case.

### 4.3 `-99.f` as a sentinel

`KI1H-KAOS.cpp:58-67` uses `-99.f` to mean "input not connected":

```cpp
if (pkIn != -99.f)
  if (pKaosTrigger.process(pkIn)) { ...
    if (bkIn == -99.f) ...
```

The call site at `:139-140` already computes `isConnected()` to produce the sentinel, so the
information is available — passing the `bool` alongside the voltage is both clearer and
removes a float equality comparison. (`-99.f` is also a legal, if unlikely, voltage.)

### 4.4 Float equality on switch values

- `KI1H_VCO.cpp:360,362` — `if (fmSwitch == 0.f)` where `fmSwitch` is an `int` (`:350`).
- `KI1H_VCO.cpp:201,209` — `if (syncType == 2.f)` where the caller passes an `int` (`:343`)
  into a `float` parameter (`:189`).
- `KI1H_FILTER.cpp:292,296` — `if (link1 == 0.f)` on values read straight from `getValue()`.

Snap-enabled switch params do land on exact integers, so these work. But the mixed int/float
comparisons obscure that, and the FILTER ones compare raw floats. Converting to `int` once
at the top of `process` and comparing integers throughout is clearer and cheaper.

### 4.5 Comment that contradicts the code

`KI1H_FILTER.cpp:132`:

```cpp
for (int i = 0; i < 12; i++) {
  ...
  // 12 simple one-poles (unrolled for efficiency)
```

The loop is not unrolled. Either unroll it (the compiler probably already does at `-O3`) or
fix the comment.

Related, `KI1H_FILTER.cpp:121` — `// Pre-calculate coefficient once per sample` — is accurate
but describes the problem in §2.4: "once per sample" is the thing to fix, not the achievement.

### 4.6 Banner comments

There are roughly 40 `// ====...` separator blocks across the tree. Many earn their place
(the section headers in `KI1H_VCO::process` genuinely help). Many don't:

- `KI1H_FILTER.cpp:7-9` — `// UTILITY FUNCTIONS` heading over an empty region.
- `KI1H_MIX.cpp:12-14`, `KI1H_ENVELOPE.cpp:9-11` — same, empty `// UTILITY FUNCTIONS` blocks.
- `KI1H_MIX.cpp:2-4`, `KI1H_VCA.cpp:2-4`, `KI1H_ENVELOPE.cpp:2-4`, `KI1H_FILTER.cpp:2-4` —
  `// INCLUDES & GLOBAL VARIABLES` in four files that have no global variables. (The one file
  that *does* have a problematic global — `KI1H_VCO.cpp:6`, see §1.2 — is the one where the
  banner would actually have been a useful warning.)
- `KI1H_MIX.cpp:127-129`, `KI1H_VCA.cpp:153-155` — `// CHANNELS - PARAMETER CONFIGURATION`
  sitting above `process`, not above any configuration.
- `KI1H_ENVELOPE.cpp:223-227` — `// PROCESS METHOD` above a commented-out line.
- `KI1H_ENVELOPE.cpp:267-269` — `// Envelope - PARAMETER CONFIGURATION` above `process`.

Several of these are stale — they describe where code *used* to be. Three lines each adds up.

### 4.7 Redundant and inconsistent includes

`rack.hpp` already pulls in `componentlibrary.hpp` and `helpers.hpp`, but they're included
explicitly in `KI1H_MIX.cpp:5-6`, `KI1H_VCA.cpp:5-6`, `KI1H_ENVELOPE.cpp:5-6` and not in
VCO, LFO, FILTER or KAOS — which compile fine, confirming they're unnecessary. They're also
included by bare name rather than path, which only resolves because of the SDK's include dirs.

`KI1H_VCA.cpp:8-10` includes `<algorithm>`, `<array>`, `<string>`; `KI1H_MIX.cpp:8-10`
includes `<array>`, `<numeric>`, `<string>`. Both use `std::accumulate`/`std::min`/`std::max`
and `std::array`, so the sets should match. (The uncommitted working changes in
`git diff` are already trimming some of these — worth finishing that pass.)

### 4.8 `addChild` where every other widget uses `addParam`

`KI1H_ENVELOPE.cpp:390-399` and `:419-428` add ten `BefacoSlidePot` param widgets with
`addChild` instead of `addParam`. The rest of the file (`:412`, `:439`) uses `addParam`, as
does every other widget in the plugin.

This *works* — `ModuleWidget::getParams()` scans children recursively
(`app/ModuleWidget.hpp:63-67`), and `addParam` is documented there as a convenience that
"just calls addChild() with additional checking." So it's not a bug. But it skips that
checking and reads as though those ten sliders are somehow different from the two toggles
beside them.

### 4.9 Vestigial code

- `KI1H_LFO.cpp:8` — `virtual float getOutput() const` on `LFO`, but `getBlink()` (`:11`) is
  non-virtual and is *also* overridden in `SampleAndHold` (`:38`), where it shadows rather
  than overrides. `LFO::process` (`:7`) is likewise hidden, not overridden, by
  `SampleAndHold::process` (`:30`) — different signature. Since both are always called through
  concrete types (`KI1H_LFO.cpp:258,275,304`), the `virtual` buys nothing and the partial
  virtuality is misleading. `LFO` also has a virtual function but no virtual destructor.
- `KI1H_ENVELOPE.cpp:34,99` — empty `ADEnvelope() {};` / `ASDEnvelope() {};` constructors.
- `KI1H_ENVELOPE.cpp:227` — `// void ADEnvelope::process() {};` commented out.
- `KI1H_ENVELOPE.cpp:251-252` — commented-out `configInput(ATK_CV, ...)` / `configInput(REL_CV, ...)`
  for inputs that don't exist in the enum.
- `KI1H_ENVELOPE.cpp:22` — `STAGE_SUSTAIN` is in the shared `Envelope::Stage` enum but is
  unreachable in `ADEnvelope`; its `evolveEnvelope` still carries an empty case for it (`:81-83`).
- `KI1H_VCO.cpp:18-20` — `Oscillator::getSin()` is used (`:356`), but `getBlink()` returning
  `blinkPhase`, which `updatePhases` (`:123`) sets equal to `phase` every call, is an alias for
  a field that already exists.
- `plugin.hpp:32-48` — `getRowPosition`/`getColumnPosition` exist only to seed `ROWS`/`COLUMNS`,
  and the comments label the arrays as "Alternative:" as though one of the pair were meant to
  be deleted. Both comments are also duplicated verbatim ("Programmatically generated row
  positions" appears above the *column* function at `:40`).
- `KI1H_VCO.cpp:244` — stray `//` on its own line mid-expression.
- **Stray semicolons** after function bodies — `};` where `}` is meant (as distinct from the
  legitimate `};` that terminates a struct definition). 26 at namespace scope:
  `KI1H_FILTER.cpp:150,230,315,400`, `KI1H_MIX.cpp:88,105,125,162,199`,
  `KI1H_LFO.cpp:113,175,241,311`, `KI1H_ENVELOPE.cpp:265,376,447`,
  `KI1H-KAOS.cpp:68,105,135,147,170`, `KI1H_VCA.cpp:100,129,151,235,274` — plus
  `KI1H_VCA.cpp:273`, which terminates a `for` loop. `KI1H_VCO.cpp` is the one file that's
  clean at this level. Also present on the inline getters inside structs:
  `KI1H_LFO.cpp:10,34,37,40`, `KI1H_MIX.cpp:32,44,47,50`, `KI1H_VCA.cpp:33,45,48`,
  `KI1H_FILTER.cpp:17`.

### 4.10 Envelope timing range doesn't reach its stated minimum

`KI1H_ENVELOPE.cpp:203-208`:

```cpp
static constexpr float minStageTime = 0.003f;  // in seconds
static constexpr float maxStageTime = 10.f;
static float convertCVToTimeInSeconds(float cv) {
  return minStageTime * std::pow(maxStageTime / minStageTime, cv);
}
```

This maps `cv ∈ [0,1]` onto `[3 ms, 10 s]`. But the params feeding it are configured
`0.1f` to `1.f` (`:234-241`), so the reachable range is `[3ms × (3333)^0.1, 10s]` ≈
`[7.4 ms, 10 s]`. The documented 3 ms minimum is unreachable.

Consequently the `clamp(params[...].getValue(), 0.f, 1.f)` calls at `:272,274,288,290,292,322,324,338,340,342`
are all no-ops — the param range is already inside `[0,1]`.

**Fix:** configure the params `0.f` to `1.f` and drop the ten redundant clamps, or adjust
`minStageTime` to match reality.

### 4.11 Output ports used as an internal signal bus

`KI1H_ENVELOPE.cpp:297` and `:347`:

```cpp
asr1TrigPulse = outputs[EOA1].getVoltage();
```

This reads back an output port to feed the next envelope stage, relying on the `setVoltage`
at line 285 having already run this sample. It works, but it couples the internal signal flow
to output-port state and to statement ordering within `process`. `ad1.eoa * CV_SCALE` is
directly available and says what it means.

### 4.12 Inconsistent link-switch polarity in FILTER

`KI1H_FILTER.cpp:226-229`:

```cpp
configSwitch(Filt1Link, 0.f, 1.f, 0.f, "Filter 1 Link", {"on", "off"});
configSwitch(Filt2Link, 0.f, 1.f, 0.f, "Filter 2 Link", {"off", "on"});
```

The two toggles are labelled with opposite polarity, and the code follows suit —
`if (link1 == 0.f)` engages linking (`:292`) while `if (link2 == 1.f)` engages it (`:296`).
So at the same physical switch position the two links mean opposite things, and both default
to their `0` position, meaning filter 1 defaults to linked and filter 2 to unlinked.

If that's intentional (matching a hardware panel), a comment saying so would save the next
reader the trip through `configSwitch`. If not, it's a bug.

---

## 5. Repo & build hygiene

### 5.1 `compile_commands.json` is committed and stale

It's tracked in git, but it lists only **3** of the 9 translation units (LFO, VCO, plugin) —
so clangd has no index for FILTER, ENVELOPE, MIX, VCA or KAOS, which is likely why several
findings above (unused params, uninitialized members) went unflagged in the editor. It also
hardcodes `/Users/waldnzwrld/Code/Rack-SDK`, so it's wrong for any other machine.

**Fix:** add to `.gitignore` (the file already ignores `/build`, `/dep`, `/dist`) and generate
it with `bear -- make` or `compiledb make`.

### 5.2 Unreferenced asset

`res/KI1H-template.svg` is tracked but referenced by no source file. Commit `09404ad`
("remove template") removed the code; this is the leftover. It also ships in the distributed
plugin, since the `Makefile:18` adds all of `res` to `DISTRIBUTABLES`.

### 5.3 No tests

There is no test target and no CI. Several of the most valuable functions here are pure and
trivially testable off the audio thread:

- `Mix::process` — would have caught §1.1 immediately.
- `softLimit`, the waveform generators, `convertCVToTimeInSeconds`, `Oscillator::calculateFreq`.
- Filter impulse/step response, which would have surfaced §1.4 and §1.5.

A single `tests/` target compiled against the DSP headers (once §3.10 extracts them) with any
header-only framework would cover all of it. That extraction is what makes testing practical —
right now the DSP is welded to the `Module` subclasses.

### 5.4 `plugin.json` metadata

`manualUrl`, `sourceUrl` and `changelogUrl` are empty (`plugin.json:11-14`). `sourceUrl` and
`changelogUrl` are required for VCV Library submission, and `license` is `"proprietary"`,
which the library also has rules around. Worth settling before release.

### 5.5 `-funsafe-math-optimizations`

This comes from the SDK (`Rack-SDK/compile.mk`), not from this project's `Makefile`, so it's
not directly changeable — but it's worth knowing that it implies `-ffinite-math-only`-adjacent
assumptions, making NaN/Inf behavior undefined. That directly compounds §1.4: an uninitialized
NaN entering a filter feedback path isn't just persistent, it's in territory where the
compiler assumes it can't happen. Initializing state properly is the mitigation.

---

## 6. Suggested order of work

**Phase 1 — correctness (small, high value, no design decisions).**
§1.1 `sizeof` → `size()`. §1.2 move `syncTrigger` into `ShaperOscillator`. §1.3 swap the
static RNG for `rack::random::normal()`. §1.4 initialize filter state and add `onReset()`.
§1.5 fix the out-of-range param defaults. §1.6 `outputs[OUT1 + i]`. Each is a few lines and
independent of the rest.

**Phase 2 — extract the shared DSP (§3.10).**
Create `src/dsp.hpp` with `softLimit`, `Channel`, the waveform generators, `Phasor`, and
`pitchToFreq`. This resolves §3.1-§3.4 and is the prerequisite for both Phase 3 and testing —
it puts each hot-path function in one place, so the optimizations below get applied once
instead of two to six times.

**Phase 3 — per-sample math.**
§2.1 `pow` → `dsp::exp2_taylor5` (now a one-line change in `pitchToFreq`). §2.3 float-precision
transcendentals. §2.4 cache filter coefficients or adopt `dsp::TBiquadFilter`. §2.5 pass arrays
by reference. §2.6 cache the S&H lag coefficient. §2.9 `static constexpr` the constants.
§2.2 (`generateShapedWave`) is the largest single win but also the only one that changes the
sound, so it wants its own pass and an A/B listen.

**Phase 4 — structural cleanup.**
§3.5/§3.6 collapse the envelope classes and the 4× `process`. §3.7 `addPanelScrews`.
§3.8/§3.9 the FILTER and VCA duplication. §2.8 disconnected-output guards — cheap, but do it
after the structure settles so the normalling paths (§2.8) are easy to see.

**Phase 5 — cosmetics and hygiene.**
Section 4 throughout (naming, the unused `Waves` enum, stray semicolons, stale banners,
vestigial code) and §5.1/§5.2. Mechanical, and best done as one sweep so the diff is easy to
skim.

**Worth doing alongside Phase 2:** stand up §5.3, even minimally. Phase 2 is exactly the point
where the DSP becomes testable, and Phase 3 and 4 are both refactors that would benefit from
having something to verify against.
