// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "dsp.hpp"
#include "plugin.hpp"

// Waveform switch positions. Order must match the configSwitch label lists in
// the constructor: WAVE_PARAM {"Triangle", "Sawtooth", "Pulse"} and
// WAVE2_PARAM {"Sin-Saw", "Pulse"}.
enum Waves { WAVE_TRI, WAVE_SAW, WAVE_SQ };
enum ShaperWaves { SHAPER_SINSAW, SHAPER_PULSE };

// ============================================================================
// OSCILLATOR BASE CLASS
// ============================================================================
/** Sub-sample position, in (-1, 0], at which a phase running at `delta` per
sample crossed threshold `t` during the sample that ended at `phase`.

`phase` is the post-wrap value in [0, 1) and `wrapped` says whether it passed
1.0 on the way. Returns 1.f when no crossing happened, which callers test with
`<= 0.f`. The result is exactly what MinBlepGenerator::insertDiscontinuity
wants, and is clamped to stay inside its required open interval. */
static float phaseCrossing(float phase, float delta, bool wrapped, float t) {
  if (delta <= 0.f)
    return 1.f;
  // Work in un-wrapped coordinates so a crossing that straddles the wrap is
  // just an ordinary interval test.
  const float end = phase + (wrapped ? 1.f : 0.f);
  const float start = end - delta;

  float hit = -1.f;
  if (start < t && t <= end)
    hit = t;
  else if (start < t + 1.f && t + 1.f <= end)
    hit = t + 1.f;
  if (hit < 0.f)
    return 1.f;

  float p = -(end - hit) / delta;
  // insertDiscontinuity requires -1 < p <= 0 and silently ignores anything
  // else, which would leave the discontinuity uncorrected.
  if (p <= -1.f)
    p = -0.999999f;
  if (p > 0.f)
    p = 0.f;
  return p;
}

struct Oscillator {
  float getOutput() const {
    return output;
  }
  float getBlink() const {
    return blinkPhase;
  }
  float getSin() const {
    return sin;
  }

  ki1h::Phasor phase;
  float output = 0.f;
  float blinkPhase = 0.f;
  float sin = 0.f;

  // Set by updatePhases, consumed by the band-limiting in the subclasses.
  float deltaPhase = 0.f;
  bool wrapped = false;

  /** phaseCrossing() for this oscillator's current step. */
  float crossing(float t) const {
    return phaseCrossing(phase.phase, deltaPhase, wrapped, t);
  }

  void updatePhases(float freq, float sampleTime);
  float calculateFreq(float pitch);
  /** The pulse-width clamp ki1h::square applies, exposed so the crossing
  detection uses the same threshold the waveform does. */
  static float clampPulseWidth(float pw) {
    return clamp(pw, 0.1f, 0.9f);
  }
};

// ============================================================================
// RAW PURE WAVEFORM OSCILLATOR
// ============================================================================
struct RawOscillator : Oscillator {
  void process(float pitch, float pulseWidth, int waveType, float sampleTime, bool needSub);
  float getSub() const {
    return sub;
  }

  ki1h::Phasor subPhase;
  float sub = 0.f;

  // One generator per discontinuous output. 16 zero-crossings at 16x
  // oversampling is what Rack's own VCO uses.
  dsp::MinBlepGenerator<16, 16> mainBlep;
  dsp::MinBlepGenerator<16, 16> subBlep;
};

// ============================================================================
// WAVESHAPING OSCILLATOR
// ============================================================================
struct ShaperOscillator : Oscillator {
  void process(float pitch, float linFM, float am, float softSync, float hardSync, float shape,
               int waveType, float sampleTime, bool needOutput);

  float generateShapedWave(float ph, float shape);
  /** The naive waveform at an arbitrary phase. Used to measure the size of the
  jump a hard-sync reset introduces. */
  float waveAt(float ph, float shape, int waveType);

  dsp::MinBlepGenerator<16, 16> blep;

  // Per-instance: the engine runs modules across worker threads, so a shared
  // trigger would both steal edges between VCOs and race on its own state.
  dsp::SchmittTrigger syncTrigger;
  float prevSyncVal = 0.f;

  // The harmonic amplitudes depend only on `shape`, which is a knob plus CV —
  // control rate, not audio rate. Cache them so the per-sample loop is
  // multiply-add only.
  static const int MAX_HARMONICS = 9;
  float harmonicCoef[MAX_HARMONICS] = {};
  int numHarmonics = 0;
  float cachedShape = -1e9f;
  void updateHarmonics(float shape);
};

// ============================================================================
// VCO MODULE DEFINITION
// ============================================================================
struct KI1H_VCO : Module {
  enum ParamIds {
    PCOARSE_PARAM,
    PFINE_PARAM,
    PULSEWIDTH_PARAM,
    WAVE_PARAM,
    SYNC_PARAM,
    FM_PARAM,
    FM_SWITCH_PARAM,
    AM_PARAM,
    PCOARSE2_PARAM,
    PFINE2_PARAM,
    SHAPE_PARAM,
    WAVE2_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    PITCH_INPUT,
    PITCH2_INPUT,
    PW1_INPUT,
    SHAPE_INPUT,
    FM_INPUT,
    AM_INPUT,
    SYNC_INPUT,
    NUM_INPUTS
  };
  enum OutputIds { WAVE_OUTPUT, WAVE2_OUTPUT, SUB_OUTPUT, NUM_OUTPUTS };
  enum LightIds { BLINK1_LIGHT, BLINK2_LIGHT, NUM_LIGHTS };

  KI1H_VCO();
  void process(const ProcessArgs &args) override;

private:
  RawOscillator osc1;
  ShaperOscillator osc2;
  static constexpr float CV_SCALE = 5.f;
  static constexpr float PWM_OFFSET = 5.5f;
};

// ============================================================================
// VCO WIDGET DEFINITION
// ============================================================================
struct KI1H_VCOWidget : ModuleWidget {
  KI1H_VCOWidget(KI1H_VCO *module);
};

// ============================================================================
// OSCILLATOR CLASS - SHARED FUNCTION
// ============================================================================
float Oscillator::calculateFreq(float pitch) {
  // Calculate frequency from pitch (1V/octave). exp2_taylor5 is accurate to
  // well under a cent over the audio range and about an order of magnitude
  // cheaper than a generic std::pow with a runtime exponent.
  return dsp::FREQ_C4 * dsp::exp2_taylor5(pitch);
}

void Oscillator::updatePhases(float freq, float sampleTime) {
  deltaPhase = freq * sampleTime;
  wrapped = phase.advance(freq, sampleTime);

  blinkPhase = phase.phase;
}

// ============================================================================
// RAWOSCILLATOR CLASS
// ============================================================================
void RawOscillator::process(float pitch, float pulseWidth, int waveType, float sampleTime,
                            bool needSub) {
  float freq = calculateFreq(pitch);

  updatePhases(freq, sampleTime);

  sin = ki1h::sine(phase.phase);

  // ==========================================================================
  // SUB OSCILLATOR (50% square, one octave down)
  // ==========================================================================
  // The sub oscillator feeds SUB_OUTPUT and nothing else, so it can be skipped
  // outright when that jack is empty. osc1's main output and sine cannot: they
  // normal into osc2's sync and FM.
  if (needSub) {
    const float subFreq = freq / 2.f;
    const float subDelta = subFreq * sampleTime;
    const bool subWrapped = subPhase.advance(subFreq, sampleTime);

    // Steps from -1 to +1 at phase 0 and back at phase 0.5.
    float sp = phaseCrossing(subPhase.phase, subDelta, subWrapped, 0.f);
    if (sp <= 0.f)
      subBlep.insertDiscontinuity(sp, 2.f);
    sp = phaseCrossing(subPhase.phase, subDelta, subWrapped, 0.5f);
    if (sp <= 0.f)
      subBlep.insertDiscontinuity(sp, -2.f);

    sub = ki1h::square(subPhase.phase) + subBlep.process();
  } else {
    sub = 0.f;
  }

  // ==========================================================================
  // MAIN WAVEFORM
  // ==========================================================================
  // Each hard edge gets a MinBLEP of the same magnitude as the jump, placed at
  // the sub-sample instant it actually happened. mainBlep.process() is called
  // exactly once per sample whatever the waveform, so switching waveform lets
  // any residual correction decay out rather than desyncing the buffer.
  switch (waveType) {
  case WAVE_TRI:
    // Triangle is continuous. Its slope discontinuity would need a MinBLAMP,
    // which the SDK does not ship; it also aliases far less (-12 dB/oct
    // against a saw's -6).
    output = ki1h::triangle(phase.phase);
    break;
  case WAVE_SAW: {
    // Falling saw: steps from -1 up to +1 at the wrap.
    const float p = crossing(0.f);
    if (p <= 0.f)
      mainBlep.insertDiscontinuity(p, 2.f);
    output = ki1h::saw(phase.phase);
    break;
  }
  case WAVE_SQ: {
    const float pw = clampPulseWidth(pulseWidth);
    const float pRise = crossing(0.f);
    if (pRise <= 0.f)
      mainBlep.insertDiscontinuity(pRise, 2.f);
    const float pFall = crossing(pw);
    if (pFall <= 0.f)
      mainBlep.insertDiscontinuity(pFall, -2.f);
    output = ki1h::square(phase.phase, pw);
    break;
  }
  default:
    output = 0.f;
  }

  output += mainBlep.process();
}

// ============================================================================
// SHAPEROSCILLATOR CLASS
// ============================================================================
void ShaperOscillator::process(float pitch, float linFM, float AM, float syncType, float syncVal,
                               float shape, int waveType, float sampleTime, bool needOutput) {
  float freq = calculateFreq(pitch);

  // Apply linear FM directly to frequency BEFORE phase update
  freq += freq * linFM * 0.1f;

  updatePhases(freq, sampleTime);
  // ============================================================================
  // SYNC PROCESSING
  // ============================================================================
  // Hard sync - digital reset when sync signal crosses threshold
  bool synced = false;
  if (syncType == 2.f) {
    if (syncTrigger.process(syncVal)) {
      // Locate the crossing of the trigger's 1.0 threshold within this sample
      // by interpolating the sync input, then correct the step the reset puts
      // in the output. Without this the reset is a raw discontinuity.
      const float before = waveAt(phase.phase, shape, waveType);
      phase.reset();
      const float after = waveAt(phase.phase, shape, waveType);

      const float rise = syncVal - prevSyncVal;
      const float frac = (rise > 0.f) ? (syncVal - 1.f) / rise : 0.f;
      float p = -clamp(frac, 0.f, 0.999999f);
      blep.insertDiscontinuity(p, after - before);
      synced = true;
    }
  }
  prevSyncVal = syncVal;

  // Soft sync - analog-modeled continuous phase pulling
  // The sync signal creates a "force" that pulls the phase toward reset
  if (syncType == 0.f) {
    float syncPull = 0.f;
    if (syncVal > 0.1f) { // Only pull when sync signal is above noise floor
      // Create exponential pull force - stronger as phase increases
      float pullStrength = syncVal * 0.2f;     // Scale sync signal
      // Quadratic pull (gets stronger near end of cycle)
      syncPull = pullStrength * phase.phase * phase.phase;

      // Pull phase backward toward 0, creating the chaotic analog behavior
      phase.phase -= syncPull * sampleTime * freq;

      // Prevent phase from going negative
      if (phase.phase < 0.f)
        phase.reset();
    }
  }

  sin = ki1h::sine(phase.phase);

  // generateShapedWave is the most expensive routine in the plugin. Skip it
  // when WAVE2_OUTPUT is empty. The phase accumulation and sync above still
  // run, so BLINK2_LIGHT keeps blinking whether or not anything is patched.
  // blep still has to be processed: hard sync inserts a discontinuity above,
  // and leaving it in the buffer would fire as a burst on reconnection.
  if (!needOutput) {
    blep.process();
    output = 0.f;
    return;
  }

  // Band-limiting. When a sync reset already happened this sample its BLEP
  // covers the jump, so the natural wrap must not be corrected as well.
  switch (waveType) {
  case SHAPER_SINSAW:
    // The Fourier series in generateShapedWave is a sum of sines and is
    // already band-limited. Its `harmonicReduction < 0.01` shortcut is not —
    // that path returns a raw rising saw, which steps from +1 down to -1.
    if (!synced && std::abs(1.f - shape) < 0.01f) {
      const float p = crossing(0.f);
      if (p <= 0.f)
        blep.insertDiscontinuity(p, -2.f);
    }
    output = generateShapedWave(phase.phase, shape);
    break;
  case SHAPER_PULSE: {
    if (!synced) {
      const float pw = clampPulseWidth(shape);
      const float pRise = crossing(0.f);
      if (pRise <= 0.f)
        blep.insertDiscontinuity(pRise, 2.f);
      const float pFall = crossing(pw);
      if (pFall <= 0.f)
        blep.insertDiscontinuity(pFall, -2.f);
    }
    output = ki1h::square(phase.phase, shape);
    break;
  }
  default:
    output = 0.f;
  }

  output += blep.process();
  output *= AM;
}

float ShaperOscillator::waveAt(float ph, float shape, int waveType) {
  switch (waveType) {
  case 0:
    return generateShapedWave(ph, shape);
  case 1:
    return ki1h::square(ph, shape);
  default:
    return 0.f;
  }
}

/** Recomputes the per-harmonic amplitudes for a given shape.

Each is (1/h) * (1 - harmonicReduction)^(h-1). Building the power by repeated
multiplication instead of std::pow removes every pow from the audio path, and
handles a negative base — reachable when shape CV pushes shape outside
[0, 2] — the same way the integer-exponent pow did. */
void ShaperOscillator::updateHarmonics(float shape) {
  cachedShape = shape;

  const float harmonicReduction = std::abs(1.f - shape);
  numHarmonics = (int)(8.f * (1.f - harmonicReduction)) + 1;
  if (numHarmonics > MAX_HARMONICS)
    numHarmonics = MAX_HARMONICS;

  const float base = 1.f - harmonicReduction;
  float gain = 1.f; // base^(h-1)
  for (int h = 1; h <= numHarmonics; h++) {
    harmonicCoef[h - 1] = gain / h; // (1/h) is the sawtooth harmonic series
    gain *= base;
  }
}

float ShaperOscillator::generateShapedWave(float ph, float shape) {
  // harmonicReduction: 0.0 = full saw, 1.0 = approaching sine
  if (std::abs(1.f - shape) < 0.01f)
    return ph * 2.f - 1.f; // Pure sawtooth

  if (shape != cachedShape)
    updateHarmonics(shape);

  // sin(h * theta) is built by angle addition from sin(theta) and cos(theta),
  // so the whole series costs one sin and one cos instead of one sin per
  // harmonic. This is an identity, not an approximation:
  //   sin((h+1)t) = sin(ht)cos(t) + cos(ht)sin(t)
  //   cos((h+1)t) = cos(ht)cos(t) - sin(ht)sin(t)
  const float theta = 2.f * M_PI * ph;
  const float s1 = std::sin(theta);
  const float c1 = std::cos(theta);

  float sh = s1, ch = c1;
  float result = 0.f;
  for (int h = 1; h <= numHarmonics; h++) {
    result += harmonicCoef[h - 1] * sh;
    const float nextS = sh * c1 + ch * s1;
    const float nextC = ch * c1 - sh * s1;
    sh = nextS;
    ch = nextC;
  }

  return result;
}

// ============================================================================
// MODULE CONSTRUCTOR - PARAMETER & I/O CONFIGURATION
// ============================================================================
KI1H_VCO::KI1H_VCO() {
  config(KI1H_VCO::NUM_PARAMS, KI1H_VCO::NUM_INPUTS, KI1H_VCO::NUM_OUTPUTS, KI1H_VCO::NUM_LIGHTS);

  // ============================================================================
  // OSCILLATOR 1 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(PFINE_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOARSE_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);
  auto waveParam =
      configSwitch(WAVE_PARAM, 0.f, 2.f, 0.f, "Wave", {"Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;

  // ============================================================================
  // OSCILLATOR 1 - INPUT/OUTPUT CONFIGURATION
  // ============================================================================
  configInput(PITCH_INPUT, "1V/oct pitch");
  configInput(PW1_INPUT, "Pulsewidth");
  configOutput(WAVE_OUTPUT, "Waveform");
  configOutput(SUB_OUTPUT, "Sub");

  // ============================================================================
  // OSCILLATOR 2 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(PFINE2_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOARSE2_PARAM, -5.5f, 6.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(SHAPE_PARAM, 0.1f, 0.9f, 0.5f, "Shape", " %", 0.f, 100.f, 0.f);
  auto waveParam2 = configSwitch(WAVE2_PARAM, 0.f, 1.f, 0.f, "Wave", {"Sin-Saw", "Pulse"});
  waveParam2->snapEnabled = true;

  // ============================================================================
  // SYNC & FM PARAMETER CONFIGURATION
  // ============================================================================
  auto syncParam = configSwitch(SYNC_PARAM, 0.f, 2.f, 1.f, "Sync", {"Weak", "OFF", "Strong"});
  syncParam->snapEnabled = true;
  configParam(FM_PARAM, 0.f, 1.f, 0.f, "FM", " %", 0.f, 100.f, 0.f);
  auto fmSwitch = configSwitch(FM_SWITCH_PARAM, 0.f, 2.f, 0.f, "FM", {"LIN", "OFF", "LOG"});
  fmSwitch->snapEnabled = true;
  configParam(AM_PARAM, 0.f, 1.f, 0.f, "AM", " %", 0.f, 100.f, 0.f);

  // ============================================================================
  // OSCILLATOR 2 - INPUT/OUTPUT CONFIGURATION
  // ============================================================================
  configInput(PITCH2_INPUT, "1V/oct pitch");
  configInput(SHAPE_INPUT, "Shape");
  configInput(SYNC_INPUT, "Ext sync");
  configInput(FM_INPUT, "FM");
  configInput(AM_INPUT, "AM");
  configOutput(WAVE2_OUTPUT, "Waveform");
}

void KI1H_VCO::process(const ProcessArgs &args) {
  // ============================================================================
  // OSCILLATOR 1 - PITCH & PWM PROCESSING
  // ============================================================================
  float pitch1 = params[PFINE_PARAM].getValue() + params[PCOARSE_PARAM].getValue();
  pitch1 += inputs[PITCH_INPUT].getVoltage();
  float pwm1 = 0;
  if (inputs[PW1_INPUT].isConnected())
    pwm1 = inputs[PW1_INPUT].getVoltage() / PWM_OFFSET;
  float pulseWidth1 = params[PULSEWIDTH_PARAM].getValue();
  int waveType1 = (int)params[WAVE_PARAM].getValue();

  // ============================================================================
  // OSCILLATOR 1 - PROCESS & OUTPUT
  // ============================================================================
  osc1.process(pitch1, pulseWidth1 + pwm1, waveType1, args.sampleTime,
               outputs[SUB_OUTPUT].isConnected());
  outputs[WAVE_OUTPUT].setVoltage(CV_SCALE * osc1.getOutput());
  outputs[SUB_OUTPUT].setVoltage(CV_SCALE * osc1.getSub());

  // ============================================================================
  // OSCILLATOR 2 - PITCH & SYNC SETUP
  // ============================================================================
  int syncType = (int)params[SYNC_PARAM].getValue();
  float pitch2 = params[PFINE2_PARAM].getValue() + params[PCOARSE2_PARAM].getValue();
  pitch2 += inputs[PITCH2_INPUT].getVoltage();

  // ============================================================================
  // OSCILLATOR 2 - FM PROCESSING
  // ============================================================================
  int fmSwitch = (int)params[FM_SWITCH_PARAM].getValue();
  // FM source selection: external input overrides internal hardwire from Osc1
  float fmVal = 1.f;
  if (inputs[FM_INPUT].isConnected())
    fmVal = inputs[FM_INPUT].getVoltage();
  else
    fmVal = osc1.getSin() * CV_SCALE;

  // FM mode switching: 0=linear, 1=off, 2=exponential
  float linFM = 0.f;
  if (fmSwitch == 0.f)
    linFM = fmVal * params[FM_PARAM].getValue();
  if (fmSwitch == 2.f)
    pitch2 += fmVal * params[FM_PARAM].getValue() * 0.2f;

  // ============================================================================
  // OSCILLATOR 2 - AM PROCESSING
  // ============================================================================
  float am = 1.f;
  if (inputs[AM_INPUT].isConnected())
    // clamp am input between 0 and 1
    am = params[AM_PARAM].getValue() * clamp(inputs[AM_INPUT].getVoltage() / CV_SCALE, 0.f, 1.f);
  // ============================================================================
  // OSCILLATOR 2 - PWM PROCESSING
  // ============================================================================
  float shapeIn = 0;
  if (inputs[SHAPE_INPUT].isConnected())
    shapeIn = inputs[SHAPE_INPUT].getVoltage() / PWM_OFFSET;

  // ============================================================================
  // OSCILLATOR 2 - SYNC PROCESSING
  // ============================================================================
  float syncVal = 0.f;
  if (inputs[SYNC_INPUT].isConnected())
    syncVal = inputs[SYNC_INPUT].getVoltage();
  else
    syncVal = (CV_SCALE * osc1.getOutput());

  // ============================================================================
  // OSCILLATOR 2 - PROCESS & OUTPUT
  // ============================================================================
  float shape = params[SHAPE_PARAM].getValue();
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  osc2.process(pitch2, linFM, am, syncType, syncVal, shape + shapeIn, waveType2, args.sampleTime,
               outputs[WAVE2_OUTPUT].isConnected());
  outputs[WAVE2_OUTPUT].setVoltage(CV_SCALE * osc2.getOutput());

  // ============================================================================
  // STATUS LIGHT PROCESSING
  // ============================================================================
  lights[BLINK1_LIGHT].setBrightness(osc1.getBlink() < 0.5f ? 1.f : 0.f);
  lights[BLINK2_LIGHT].setBrightness(osc2.getBlink() < 0.5f ? 1.f : 0.f);
}

KI1H_VCOWidget::KI1H_VCOWidget(KI1H_VCO *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-VCO.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);

  // ============================================================================
  // OSCILLATOR 1 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_VCO::PFINE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                               KI1H_VCO::PCOARSE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                               KI1H_VCO::PULSEWIDTH_PARAM));

  // ============================================================================
  // OSCILLATORS - STATUS LIGHT
  // ============================================================================
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[1] - HALF_R)), module, KI1H_VCO::BLINK1_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[5] - HALF_R)), module, KI1H_VCO::BLINK2_LIGHT));

  // ============================================================================
  // OSCILLATOR 1 - INPUTS, CONTROLS & OUTPUT
  // ============================================================================
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                             KI1H_VCO::PITCH_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                             KI1H_VCO::WAVE_PARAM));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                             KI1H_VCO::PW1_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3] - HALF_C / 2, ROWS[1] - HALF_R)),
                                             module, KI1H_VCO::WAVE_OUTPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3] - HALF_C / 2, ROWS[2] - HALF_R)),
                                             module, KI1H_VCO::SUB_OUTPUT));

  // ============================================================================
  // OSCILLATOR 2 - SYNC & FM CONTROLS
  // ============================================================================
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[3])), module, KI1H_VCO::SYNC_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[3])), module,
                                             KI1H_VCO::SYNC_PARAM));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[2])), module,
                                             KI1H_VCO::FM_SWITCH_PARAM));

  // ============================================================================
  // OSCILLATOR 2 - CONTROL KNOBS & OUTPUT
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                               KI1H_VCO::PFINE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[4])), module,
                                               KI1H_VCO::PCOARSE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                               KI1H_VCO::SHAPE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[2])), module,
                                               KI1H_VCO::FM_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[3])), module,
                                               KI1H_VCO::AM_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3] - HALF_C / 2, ROWS[5] - HALF_R)),
                                             module, KI1H_VCO::WAVE2_OUTPUT));

  // ============================================================================
  // OSCILLATOR 2 - INPUTS & WAVE CONTROL
  // ============================================================================
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                             KI1H_VCO::PITCH2_INPUT));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                             KI1H_VCO::WAVE2_PARAM));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                             KI1H_VCO::SHAPE_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[2])), module,
                                             KI1H_VCO::FM_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[3] - HALF_C / 2, ROWS[3] - HALF_R)),
                                             module, KI1H_VCO::AM_INPUT));
}

Model *modelKI1H_VCO = createModel<KI1H_VCO, KI1H_VCOWidget>("KI1H-VCO");
