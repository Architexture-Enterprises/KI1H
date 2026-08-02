#pragma once
#include "plugin.hpp"
#include <cmath>

/** Shared DSP primitives.

Everything here was duplicated across two or more modules. Collecting it in one
place means a hot-path optimization gets applied once instead of two to six
times, and it makes these functions testable off the audio thread — they are
pure, or own nothing but their own state.

The SDK pins -std=c++11, so no std::clamp, if constexpr or structured
bindings. rack::clamp and rack::simd are available.
*/
namespace ki1h {

/** Single-precision pi. The <cmath> M_PI is a double, and multiplying a float
phase by it promotes the whole expression to double, calls the double overload
of sin/exp, then narrows back. */
static constexpr float PI = 3.14159265358979323846f;

/** Output voltage scalings used across the modules. */
static constexpr float CV_SCALE_5V = 5.f;
static constexpr float CV_SCALE_10V = 10.f;

/** Soft saturation above +/-5.2 V, approaching the rail exponentially. */
inline float softLimit(float input) {
  if (std::fabs(input) > 5.2f) {
    float sign = (input >= 0) ? 1.0f : -1.0f;
    float excess = std::fabs(input) - 5.2f;
    return sign * (5.2f + excess * std::exp(-excess * 2.0f));
  }
  return input;
}

/** Converts a 1V/octave pitch to Hz.

exp2_taylor5 has at most 6e-06 relative error — well under a cent — and is
about an order of magnitude cheaper than std::pow with a runtime exponent. Its
exponent-field step is valid for pitch in [-127, 128]. */
inline float pitchToFreq(float pitch) {
  return dsp::FREQ_C4 * dsp::exp2_taylor5(pitch);
}

/** A phase accumulator normalized to [0, 1). */
struct Phasor {
  float phase = 0.f;

  /** Advances by freq * dt and wraps. Returns whether the phase passed 1.0
  during the step, which the band-limiting in the VCO needs to place its
  discontinuity corrections.

  The wrap is a floor, not a single subtraction. `if (phase >= 1) phase -= 1`
  only handles phase < 2, so at very high frequencies or very low sample rates
  — where freq * dt exceeds 1 — it leaves the phase above 1 and the oscillator
  silently breaks. */
  bool advance(float freq, float dt) {
    phase += freq * dt;
    float wraps = std::floor(phase);
    phase -= wraps;
    return wraps > 0.f;
  }

  void reset() {
    phase = 0.f;
  }
};

// ============================================================================
// WAVEFORM GENERATORS
// All take a phase in [0, 1) and return [-1, +1].
// ============================================================================

inline float sine(float ph) {
  return std::sin(2.f * PI * ph);
}

inline float triangle(float ph) {
  if (ph < 0.5f)
    return ph * 4.f - 1.f; // Rising: 0->0.5 becomes -1->+1
  else
    return 3.f - ph * 4.f; // Falling: 0.5->1 becomes +1->-1
}

inline float saw(float ph) {
  return ph * -2.f + 1.f; // Falling: maps 0->1 phase to +1->-1
}

inline float ramp(float ph) {
  return ph * 2.f - 1.f; // Rising: maps 0->1 phase to -1->+1
}

/** Pulse wave. pw is clamped away from the extremes, where the wave would
degenerate to a constant. The default is the plain 50% square. */
inline float square(float ph, float pw = 0.5f) {
  pw = clamp(pw, 0.1f, 0.9f);
  return (ph > pw) ? -1.f : 1.f;
}

/** One mixer/VCA channel: a gain stage into the soft limiter. */
struct Channel {
  float output = 0.f;

  void process(float input, float cvIn) {
    output = softLimit(input * cvIn);
  }

  float getOutput() const {
    return output;
  }
};

} // namespace ki1h
