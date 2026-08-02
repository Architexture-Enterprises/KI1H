/* Unit tests for the pure DSP primitives in src/dsp.hpp.

Run with `make test RACK_DIR=/path/to/Rack-SDK`.

No test framework is vendored. The SDK pins -std=c++11, and the whole suite
needs two assertions and a float comparison, so a ~40-line harness costs less
than dropping a 15,000-line single-header framework into the repo.

Scope: everything in src/dsp.hpp. The other functions the issue names —
Mix::process, KI1H_ENVELOPE::convertCVToTimeInSeconds,
Oscillator::calculateFreq, and the filter step responses — are still welded to
their Module subclasses in the .cpp files, so they are not reachable from here
yet. Each becomes testable when its own extraction lands; see issues #43, #54,
#55 and #57. */

#include "dsp.hpp"
#include <cmath>
#include <cstdio>

// ============================================================================
// HARNESS
// ============================================================================
static int checks = 0;
static int failures = 0;

static void check(bool ok, const char *expr, const char *file, int line) {
  checks++;
  if (!ok) {
    failures++;
    std::printf("FAIL %s:%d  %s\n", file, line, expr);
  }
}

static void checkNear(float got, float want, float tol, const char *expr, const char *file,
                      int line) {
  checks++;
  if (!(std::fabs(got - want) <= tol)) {
    failures++;
    std::printf("FAIL %s:%d  %s\n     got %.9g, want %.9g (tol %g)\n", file, line, expr, got, want,
                tol);
  }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(got, want, tol) checkNear((got), (want), (tol), #got " ~= " #want, __FILE__, __LINE__)

// ============================================================================
// softLimit
// ============================================================================
static void testSoftLimit() {
  // Transparent inside the linear region.
  CHECK_NEAR(ki1h::softLimit(0.f), 0.f, 0.f);
  CHECK_NEAR(ki1h::softLimit(1.f), 1.f, 0.f);
  CHECK_NEAR(ki1h::softLimit(-1.f), -1.f, 0.f);
  CHECK_NEAR(ki1h::softLimit(5.2f), 5.2f, 0.f);

  // Above the knee it compresses but never inverts or exceeds the asymptote.
  CHECK(ki1h::softLimit(6.f) > 5.2f);
  CHECK(ki1h::softLimit(6.f) < 6.f);
  CHECK_NEAR(ki1h::softLimit(6.f), 5.2f + 0.8f * std::exp(-1.6f), 1e-5f);

  // Far outside, output collapses onto the 5.2 asymptote.
  CHECK_NEAR(ki1h::softLimit(1000.f), 5.2f, 1e-4f);

  // Odd symmetry.
  for (int i = 0; i < 200; i++) {
    float x = i * 0.25f;
    CHECK_NEAR(ki1h::softLimit(-x), -ki1h::softLimit(x), 1e-6f);
  }

  // Bounded: never exceeds the peak of the curve, never inverts sign.
  for (int i = -5000; i <= 5000; i++) {
    float x = i * 0.5f;
    float y = ki1h::softLimit(x);
    CHECK(std::fabs(y) <= 5.3840f);
    CHECK(x == 0.f || (y > 0.f) == (x > 0.f));
    if (failures)
      return;
  }

  // CHARACTERIZATION, not an endorsement. excess * exp(-2 * excess) peaks at
  // excess = 0.5 and decays after it, so this curve is NOT monotonic: it rises
  // to 5.3839 at 5.7 V and then falls back toward 5.2. Between roughly 5.7 V
  // and 10 V a louder input therefore produces a *quieter* output.
  //
  // These assertions pin the behaviour as it stands so a future change to the
  // limiter is a deliberate decision rather than an accident. If the fold-back
  // is unwanted, `5.2 + excess / (1 + 2 * excess)` is monotonic, has the same
  // slope at the knee, and asymptotes to 5.7.
  CHECK_NEAR(ki1h::softLimit(5.7f), 5.3839f, 1e-3f);         // the peak
  CHECK(ki1h::softLimit(6.f) < ki1h::softLimit(5.7f));       // already falling
  CHECK(ki1h::softLimit(7.f) < ki1h::softLimit(6.f));        // still falling
  CHECK(ki1h::softLimit(20.f) < ki1h::softLimit(7.f));       // down to the floor
  CHECK_NEAR(ki1h::softLimit(20.f), 5.2f, 1e-4f);

  // Monotonic everywhere below the knee, which is the region that matters for
  // normal signal levels.
  float prev = ki1h::softLimit(-5.2f);
  for (int i = -519; i <= 520; i++) {
    float y = ki1h::softLimit(i * 0.01f);
    CHECK(y >= prev);
    prev = y;
    if (failures)
      return;
  }
}

// ============================================================================
// Phasor
// ============================================================================
static void testPhasor() {
  ki1h::Phasor p;
  CHECK_NEAR(p.phase, 0.f, 0.f);

  // Ordinary accumulation.
  p.advance(0.25f, 1.f);
  CHECK_NEAR(p.phase, 0.25f, 1e-6f);
  p.advance(0.25f, 1.f);
  CHECK_NEAR(p.phase, 0.5f, 1e-6f);

  // Wrapping at 1.0.
  p.advance(0.75f, 1.f);
  CHECK_NEAR(p.phase, 0.25f, 1e-6f);

  // Regression for the single-subtraction wrap this replaced: with
  // `if (phase >= 1) phase -= 1`, an increment above 1.0 leaves the phase out
  // of range and the oscillator silently dies.
  ki1h::Phasor big;
  big.advance(3000.f, 1.f / 1000.f); // increment of 3.0
  CHECK(big.phase >= 0.f);
  CHECK(big.phase < 1.f);

  // Same, at an increment that is not a whole number of cycles.
  ki1h::Phasor big2;
  big2.advance(2500.f, 1.f / 1000.f); // increment of 2.5
  CHECK_NEAR(big2.phase, 0.5f, 1e-5f);

  // Stays in range over a long run at an awkward ratio.
  ki1h::Phasor run;
  for (int i = 0; i < 100000; i++) {
    run.advance(997.f, 1.f / 44100.f);
    CHECK(run.phase >= 0.f && run.phase < 1.f);
    if (failures)
      return; // don't print 100k identical failures
  }

  run.reset();
  CHECK_NEAR(run.phase, 0.f, 0.f);
}

// ============================================================================
// Waveform generators
// ============================================================================
static void testWaveforms() {
  // sine: zero at 0 and 0.5, +1 at 0.25, -1 at 0.75.
  CHECK_NEAR(ki1h::sine(0.f), 0.f, 1e-6f);
  CHECK_NEAR(ki1h::sine(0.25f), 1.f, 1e-6f);
  CHECK_NEAR(ki1h::sine(0.5f), 0.f, 1e-6f);
  CHECK_NEAR(ki1h::sine(0.75f), -1.f, 1e-6f);

  // triangle: -1 at 0, +1 at 0.5, 0 at both quarter points.
  CHECK_NEAR(ki1h::triangle(0.f), -1.f, 1e-6f);
  CHECK_NEAR(ki1h::triangle(0.25f), 0.f, 1e-6f);
  CHECK_NEAR(ki1h::triangle(0.5f), 1.f, 1e-6f);
  CHECK_NEAR(ki1h::triangle(0.75f), 0.f, 1e-6f);

  // saw falls +1 -> -1; ramp rises -1 -> +1. They are mirrors.
  CHECK_NEAR(ki1h::saw(0.f), 1.f, 1e-6f);
  CHECK_NEAR(ki1h::saw(0.5f), 0.f, 1e-6f);
  CHECK_NEAR(ki1h::ramp(0.f), -1.f, 1e-6f);
  CHECK_NEAR(ki1h::ramp(0.5f), 0.f, 1e-6f);
  for (int i = 0; i <= 100; i++) {
    float ph = i * 0.01f;
    CHECK_NEAR(ki1h::saw(ph), -ki1h::ramp(ph), 1e-6f);
  }

  // square defaults to a 50% duty cycle.
  CHECK_NEAR(ki1h::square(0.f), 1.f, 0.f);
  CHECK_NEAR(ki1h::square(0.49f), 1.f, 0.f);
  CHECK_NEAR(ki1h::square(0.51f), -1.f, 0.f);

  // A supplied pulse width moves the transition.
  CHECK_NEAR(ki1h::square(0.2f, 0.25f), 1.f, 0.f);
  CHECK_NEAR(ki1h::square(0.3f, 0.25f), -1.f, 0.f);

  // Extreme widths are clamped to [0.1, 0.9] so the wave never degenerates to
  // a constant.
  CHECK_NEAR(ki1h::square(0.05f, 0.f), 1.f, 0.f);  // pw clamped up to 0.1
  CHECK_NEAR(ki1h::square(0.95f, 1.f), -1.f, 0.f); // pw clamped down to 0.9

  // Every generator stays inside [-1, +1] across a full cycle.
  for (int i = 0; i < 1000; i++) {
    float ph = i * 0.001f;
    CHECK(std::fabs(ki1h::sine(ph)) <= 1.f + 1e-6f);
    CHECK(std::fabs(ki1h::triangle(ph)) <= 1.f + 1e-6f);
    CHECK(std::fabs(ki1h::saw(ph)) <= 1.f + 1e-6f);
    CHECK(std::fabs(ki1h::ramp(ph)) <= 1.f + 1e-6f);
    CHECK(std::fabs(ki1h::square(ph)) <= 1.f + 1e-6f);
    if (failures)
      return;
  }
}

// ============================================================================
// pitchToFreq
// ============================================================================
static void testPitchToFreq() {
  // 0 V is C4 by definition.
  CHECK_NEAR(ki1h::pitchToFreq(0.f), 261.6256f, 1e-2f);

  // 1 V/octave.
  CHECK_NEAR(ki1h::pitchToFreq(1.f), 523.2512f, 1e-2f);
  CHECK_NEAR(ki1h::pitchToFreq(-1.f), 130.8128f, 1e-2f);
  CHECK_NEAR(ki1h::pitchToFreq(4.f), 261.6256f * 16.f, 1.f);

  // Accurate to well under a cent against the exact std::pow across the range
  // the modules actually use. A cent is a factor of 2^(1/1200).
  const float cent = 0.0005946f; // 2^(1/1200) - 1
  for (int i = -100; i <= 100; i++) {
    float pitch = i * 0.1f;
    float exact = 261.6256f * std::pow(2.f, pitch);
    float approx = ki1h::pitchToFreq(pitch);
    CHECK(std::fabs(approx - exact) / exact < cent);
    if (failures)
      return;
  }
}

// ============================================================================
// Channel
// ============================================================================
static void testChannel() {
  ki1h::Channel c;
  CHECK_NEAR(c.getOutput(), 0.f, 0.f); // initialized, not indeterminate

  c.process(1.f, 1.f);
  CHECK_NEAR(c.getOutput(), 1.f, 1e-6f);

  // Unity CV passes the signal through.
  c.process(3.f, 1.f);
  CHECK_NEAR(c.getOutput(), 3.f, 1e-6f);

  // Zero CV mutes.
  c.process(5.f, 0.f);
  CHECK_NEAR(c.getOutput(), 0.f, 1e-6f);

  // Negative CV inverts.
  c.process(2.f, -1.f);
  CHECK_NEAR(c.getOutput(), -2.f, 1e-6f);

  // The gain stage goes through the soft limiter, so it cannot run away.
  // 10 * 10 = 100 V is far past the knee, where softLimit has collapsed onto
  // its 5.2 asymptote (see testSoftLimit).
  c.process(10.f, 10.f);
  CHECK_NEAR(c.getOutput(), 5.2f, 1e-4f);
  CHECK_NEAR(c.getOutput(), ki1h::softLimit(100.f), 1e-6f);

  // Just past the knee it still compresses rather than clipping flat.
  c.process(5.7f, 1.f);
  CHECK(c.getOutput() > 5.2f);
  CHECK(c.getOutput() < 5.7f);
}

int main() {
  testSoftLimit();
  testPhasor();
  testWaveforms();
  testPitchToFreq();
  testChannel();

  std::printf("\n%d checks, %d failure%s\n", checks, failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
