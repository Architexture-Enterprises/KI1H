#include "dsp.hpp"
#include "plugin.hpp"

// Waveform switch positions. Order must match the configSwitch label lists in
// the constructor: WAVE1_PARAM / WAVE2_PARAM {"Sine", "Sawtooth", "Pulse"} and
// SWAVE_PARAM {"Sawtooth", "Ramp", "Triangle"}.
enum LFOWaves { LFO_SINE, LFO_SAW, LFO_SQUARE };
enum SHWaves { SH_SAW, SH_RAMP, SH_TRIANGLE };

// External-clock multiplier/divider. Whenever CLOCK_INPUT is patched, the Sample
// Rate knob is repurposed as a mult/div selector: its travel is quantized to
// seven power-of-two ratios with the centre detent at unity.
//   exp:   -3  -2  -1   0  +1  +2  +3
//   ratio  /8  /4  /2  x1  x2  x4  x8
static constexpr float SRATE_MIN = -10.f;
static constexpr float SRATE_MAX = -3.4f;
static constexpr int CLOCK_RATIO_STEPS = 7;

// Maps a normalized [0,1] knob position to a ratio exponent in [-3, +3].
static int clockRatioExp(float norm) {
  int bucket = clamp((int)std::round(norm * (CLOCK_RATIO_STEPS - 1)), 0, CLOCK_RATIO_STEPS - 1);
  return bucket - (CLOCK_RATIO_STEPS - 1) / 2;
}

// ============================================================================
// LFO CLASS DEFINITION
// ============================================================================
// Note: SampleAndHold below hides rather than overrides process(), getOutput()
// and getBlink(). That is fine because both are only ever used through their
// concrete types; nothing calls them through an LFO*. Marking one of the three
// virtual bought nothing and implied a polymorphism that does not exist.
struct LFO {
  void process(float pitch, int waveType, float sampletime);
  float getOutput() const {
    return output;
  }
  float getBlink() const {
    return phase.phase;
  }

  float output = 0.f;
  ki1h::Phasor phase;
};

// ============================================================================
// SAMPLE AND HOLD CLASS DEFINITION (Inherits from LFO)
// ============================================================================
struct SampleAndHold : LFO {
public:
  void process(float oscPhase, float clockIn, float sampleRate, int ratioExp, float sampleIn,
               bool sampInConn, int waveType, float lagTime, float sampleTime, bool needOutput);
  float getOutput() const {
    return laggedOutput;
  }
  float getClock() const {
    return clockOutput;
  }

  ki1h::Phasor clockPhase;
  float sampledValue = 0.f;
  float laggedOutput = 0.f;
  float clockOutput = 0.f;
  dsp::SchmittTrigger sampleTrigger;
  // Squares an external clock: turns any input waveform into the gate state that
  // drives the 0-10 V clock output.
  dsp::SchmittTrigger extClockGate;

  // External-clock multiplier/divider state (used only while a clock is patched).
  ki1h::Phasor ratioPhase;   // phase-locked generator for the x N / / N output
  float estClockFreq = 0.f;  // Hz, estimated from the last measured input period
  float timeSinceEdge = 0.f; // seconds since the last input rising edge
  int divCounter = 0;        // input edges counted toward the next / N output edge
  int cachedRatioExp = 99;   // last ratio, so a knob change re-locks the generator

  // Cached lag coefficient. lagTime is a knob and sampleTime only moves on a
  // sample-rate change, so the exp() behind it almost never needs redoing.
  // The sentinels are negative so the first process() call always misses.
  float lagAlpha = 0.f;
  float cachedLagTime = -1.f;
  float cachedSampleTime = -1.f;
};

// ============================================================================
// LFO MODULE DEFINITION
// ============================================================================
struct KI1H_LFO : Module {
  enum ParamIds {
    RATE1_PARAM,
    RATE1CV_PARAM,
    WAVE1_PARAM,
    RATE2_PARAM,
    RATE2CV_PARAM,
    WAVE2_PARAM,
    SRATE_PARAM,
    SWAVE_PARAM,
    SLAG_PARAM,
    NUM_PARAMS
  };
  enum InputIds { CV1_INPUT, CV2_INPUT, SAMP_INPUT, CLOCK_INPUT, NUM_INPUTS };
  enum OutputIds { WAVE1_OUTPUT, WAVE2_OUTPUT, CLOCK_OUTPUT, SWAVE_OUTPUT, NUM_OUTPUTS };
  enum LightIds { BLINK1_LIGHT, BLINK2_LIGHT, CLOCK_LIGHT, NUM_LIGHTS };

  KI1H_LFO();
  void process(const ProcessArgs &args) override;

private:
  LFO lfo1, lfo2;
  SampleAndHold SNH;
  static constexpr float CV_SCALE = 5.f;
};

// When a clock is patched, the Sample Rate knob is a mult/div selector, so its
// tooltip should read out the ratio rather than a meaningless free-run frequency.
struct ClockRateQuantity : ParamQuantity {
  std::string getString() override {
    if (module && module->inputs[KI1H_LFO::CLOCK_INPUT].isConnected()) {
      int exp = clockRatioExp(getScaledValue());
      std::string ratio;
      if (exp == 0)
        ratio = "x1";
      else if (exp > 0)
        ratio = string::f("x%d", 1 << exp);
      else
        ratio = string::f("/%d", 1 << (-exp));
      return "Clock ratio: " + ratio;
    }
    return ParamQuantity::getString();
  }
};

// ============================================================================
// LFO WIDGET DEFINITION
// ============================================================================
struct KI1H_LFOWidget : ModuleWidget {
  KI1H_LFOWidget(KI1H_LFO *module);
};
void LFO::process(float pitch, int waveType, float sampleTime) {

  float freq = dsp::FREQ_C4 * dsp::exp2_taylor5(pitch);

  // ============================================================================
  // PHASE ACCUMULATION
  // ============================================================================
  // Normal phase accumulation
  phase.advance(freq, sampleTime);

  // ============================================================================
  // WAVEFORM GENERATION
  // ============================================================================
  // Generate waveform based on type
  switch (waveType) {
  case LFO_SINE:
    output = ki1h::sine(phase.phase);
    break;
  case LFO_SAW:
    output = ki1h::saw(phase.phase);
    break;
  case LFO_SQUARE:
    output = ki1h::square(phase.phase);
    break;
  default:
    output = 0.f;
  }
}

// ============================================================================
// SAMPLE AND HOLD PROCESS METHOD
// ============================================================================
void SampleAndHold::process(float oscPhase, float clockIn, float sampleRate, int ratioExp,
                            float sampleIn, bool sampInConn, int sWaveType, float lagTime,
                            float sampleTime, bool needOutput) {

  float clockFreq = dsp::FREQ_C4 * dsp::exp2_taylor5(sampleRate);
  // ============================================================================
  // PHASE ACCUMULATION
  // ============================================================================
  // The clock half always runs: it drives CLOCK_OUTPUT and CLOCK_LIGHT.
  clockPhase.advance(clockFreq, sampleTime);

  // The clock output is always a clean 0-10 V square. Internally it free-runs off
  // clockPhase; with a clock patched it squares that input — a bipolar sine, or a
  // hot/uneven external square — into the same 0-10 V gate via hysteresis
  // thresholds, so the output level never depends on the input's amplitude.
  if (sampleRate == -1) {
    // External clock patched: the Sample Rate knob is a mult/div selector.
    bool rising = extClockGate.process(clockIn, 0.1f, 1.f);
    timeSinceEdge += sampleTime;

    // Re-lock the generator cleanly whenever the selected ratio changes, so a
    // knob turn snaps to the new ratio instead of drifting in from the old phase.
    if (ratioExp != cachedRatioExp) {
      cachedRatioExp = ratioExp;
      ratioPhase.reset();
      divCounter = 0;
    }

    if (rising) {
      // Estimate the input clock rate from the period that just completed.
      if (timeSinceEdge > 0.f)
        estClockFreq = 1.f / timeSinceEdge;
      timeSinceEdge = 0.f;
    }

    if (ratioExp == 0) {
      // x1: an exact, jitter-free clone of the squared input.
      clockOutput = extClockGate.isHigh() ? 10.f : 0.f;
    } else if (estClockFreq <= 0.f) {
      // No period measured yet: hold low until the input clock is running.
      clockOutput = 0.f;
    } else if (ratioExp > 0) {
      // x N: generator at N times the input rate, hard-synced to every input edge
      // so its faster pulses stay phase-locked to the incoming clock.
      ratioPhase.advance(estClockFreq * (float)(1 << ratioExp), sampleTime);
      if (rising)
        ratioPhase.phase = 0.f;
      clockOutput = ki1h::square(ratioPhase.phase) > 0.f ? 10.f : 0.f;
    } else {
      // / N: one output period spans N input periods. Free-run at 1/N the input
      // rate, re-syncing on every N-th input edge to hold the division locked.
      int div = 1 << (-ratioExp);
      ratioPhase.advance(estClockFreq / (float)div, sampleTime);
      if (rising && ++divCounter >= div) {
        divCounter = 0;
        ratioPhase.phase = 0.f;
      }
      clockOutput = ki1h::square(ratioPhase.phase) > 0.f ? 10.f : 0.f;
    }
  } else {
    cachedRatioExp = 99; // force a clean re-lock when a clock is next patched
    clockOutput = ki1h::square(clockPhase.phase) > 0.f ? 10.f : 0.f;
  }

  // Everything below feeds SWAVE_OUTPUT only, so it can be skipped when that jack
  // is empty — saving the waveform generator, a Schmitt trigger and an exp.
  if (!needOutput)
    return;

  // The S&H oscillator runs at lfo2's pitch, so it takes lfo2's phase directly
  // rather than accumulating a bit-identical copy of it (and paying a second
  // exp2 per sample to do so). The clock phase above is genuinely independent.
  phase.phase = oscPhase;

  // ============================================================================
  // S&H SPECIFIC WAVEFORM GENERATION
  // ============================================================================
  // Generate S&H waveforms (different from regular LFO waveforms)
  switch (sWaveType) {
  case SH_SAW:
    output = ki1h::saw(phase.phase);
    break;
  case SH_RAMP:
    output = ki1h::ramp(phase.phase);
    break;
  case SH_TRIANGLE:
    output = ki1h::triangle(phase.phase);
    break;
  default:
    output = 0.f;
  }

  // ============================================================================
  // SAMPLE ON TRIGGER RISING EDGE
  // ============================================================================
  // The S&H tracks the raw incoming clock unmodified: with an external clock
  // patched it samples on the input's own edges, independent of the mult/div
  // ratio applied to CLOCK_OUTPUT. Free-running, it follows the internal clock.
  float sampleClock = (sampleRate == -1) ? (extClockGate.isHigh() ? 10.f : 0.f) : clockOutput;
  // Use Schmitt trigger for robust edge detection with hysteresis
  if (sampleTrigger.process(sampleClock)) {
    // Sample the current oscillator output value on rising edge
    sampledValue = sampInConn ? sampleIn : output;
  }

  // ============================================================================
  // APPLY EXPONENTIAL LAG TO SAMPLED VALUE
  // ============================================================================
  // Recompute only when the knob or the sample rate actually moves.
  if (lagTime != cachedLagTime || sampleTime != cachedSampleTime) {
    cachedLagTime = lagTime;
    cachedSampleTime = sampleTime;
    // Time constant for 99% settling in lagTime
    float timeConstant = lagTime / 4.605f;
    lagAlpha = 1.0f - std::exp(-sampleTime / timeConstant);
  }

  // Apply lag filtering to the sampled value
  laggedOutput = lagAlpha * sampledValue + (1.0f - lagAlpha) * laggedOutput;
}

KI1H_LFO::KI1H_LFO() {
  // ============================================================================
  // MODULE CONFIGURATION
  // ============================================================================
  config(KI1H_LFO::NUM_PARAMS, KI1H_LFO::NUM_INPUTS, KI1H_LFO::NUM_OUTPUTS, KI1H_LFO::NUM_LIGHTS);

  // ============================================================================
  // LFO 1 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(RATE1_PARAM, -10.f, -3.4f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(RATE1CV_PARAM, 0.f, 1.f, 1.f, "Rate CV Scale", "%", 0.f, 100, 0.f);
  configInput(CV1_INPUT, "Rate");
  auto waveParam = configSwitch(WAVE1_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sine", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;
  configOutput(WAVE1_OUTPUT, "LFO1 Out");

  // ============================================================================
  // LFO 2 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(RATE2_PARAM, -10.f, -3.4f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(RATE2CV_PARAM, 0.f, 1.f, 1.f, "Rate CV Scale", "%", 0.f, 100, 0.f);
  configInput(CV2_INPUT, "Rate");
  auto wave2Param = configSwitch(WAVE2_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sine", "Sawtooth", "Pulse"});
  wave2Param->snapEnabled = true;
  configOutput(WAVE2_OUTPUT, "LFO2 Out");

  // ============================================================================
  // S&H - PARAMETER CONFIGURATION
  // ============================================================================
  configParam<ClockRateQuantity>(SRATE_PARAM, SRATE_MIN, SRATE_MAX, -5.3f, "Sample Rate", "Hz", 2.f,
                                 dsp::FREQ_C4, 0.f);
  auto sWaveParam =
      configSwitch(SWAVE_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sawtooth", "Ramp", "Triangle"});
  sWaveParam->snapEnabled = true;
  configParam(SLAG_PARAM, 0.0f, 0.5f, 0.f, "Lag", "ms", 0.f, 1000.f, 0.f);
  configInput(SAMP_INPUT, "Ext. In");
  configInput(CLOCK_INPUT, "Clock in");
  configOutput(SWAVE_OUTPUT, "S&H Out");
  configOutput(CLOCK_OUTPUT, "Clock Out");
}

void KI1H_LFO::process(const ProcessArgs &args) {
  // ============================================================================
  // LFO 1 - PITCH
  // ============================================================================
  float pitch1 = params[RATE1_PARAM].getValue();
  // Scale CV input by RATE1CV_PARAM param (0.01 to 1.0 range)
  if (inputs[CV1_INPUT].isConnected()) {
    float cvScale = 0.01f + params[RATE1CV_PARAM].getValue() * 0.99f; // Map 0-1 param to 0.01-1.0 scale
    pitch1 += inputs[CV1_INPUT].getVoltage() * cvScale;
  }
  int waveType1 = (int)params[WAVE1_PARAM].getValue();

  // ============================================================================
  // LFO 1 - PROCESS & OUTPUT
  // ============================================================================
  lfo1.process(pitch1, waveType1, args.sampleTime);
  outputs[WAVE1_OUTPUT].setVoltage(CV_SCALE * lfo1.getOutput());

  // ============================================================================
  // LFO 2 - PITCH
  // ============================================================================
  float pitch2 = params[RATE2_PARAM].getValue();
  // Scale CV input by RATE2CV_PARAM param (0.01 to 1.0 range)
  if (inputs[CV2_INPUT].isConnected()) {
    float cvScale = 0.01f + params[RATE2CV_PARAM].getValue() * 0.99f; // Map 0-1 param to 0.01-1.0 scale
    pitch2 += inputs[CV2_INPUT].getVoltage() * cvScale;
  }
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  // ============================================================================
  // LFO 2 - PROCESS & OUTPUT
  // ============================================================================
  lfo2.process(pitch2, waveType2, args.sampleTime);
  outputs[WAVE2_OUTPUT].setVoltage(CV_SCALE * lfo2.getOutput());

  // ============================================================================
  // S&H - PARAMETERS & PROCESSING
  // ============================================================================
  float sRate = params[SRATE_PARAM].getValue();
  int sWaveType = (int)params[SWAVE_PARAM].getValue();
  float lagTime = params[SLAG_PARAM].getValue();

  // Ensure minimum lag time to prevent division by zero
  lagTime = std::max(lagTime, 0.001f);

  // ============================================================================
  // SNH - PROCESS & OUTPUT
  // ============================================================================
  // Sample and Hold with Lag - Implementation details:
  // 1. Uses LFO2 oscillator as trigger source
  // 2. Samples on rising edge crossings (negative to positive)
  // 3. Applies exponential lag with tau = lagTime / 4.605 for 99% settling
  // 4. Models analog RC circuit with JFET buffer behavior
  float sampleIn = 0.f;
  bool ext = inputs[SAMP_INPUT].isConnected();
  if (ext)
    sampleIn = inputs[SAMP_INPUT].getVoltage() * 0.2f;
  float clockIn = inputs[CLOCK_INPUT].getVoltage();
  bool clockConn = inputs[CLOCK_INPUT].isConnected();
  int ratioExp = 0;
  if (clockConn) {
    sRate = -1.f;
    // Repurpose the Sample Rate knob as the mult/div selector (centre = x1).
    float norm = (params[SRATE_PARAM].getValue() - SRATE_MIN) / (SRATE_MAX - SRATE_MIN);
    ratioExp = clockRatioExp(norm);
  }

  // lfo2.process() above has already advanced lfo2.phase for this sample.
  SNH.process(lfo2.phase.phase, clockIn, sRate, ratioExp, sampleIn, ext, sWaveType, lagTime,
              args.sampleTime, outputs[SWAVE_OUTPUT].isConnected());
  outputs[SWAVE_OUTPUT].setVoltage(CV_SCALE * SNH.getOutput());
  // getClock() already returns the finished 0-10 V square, so no CV_SCALE here.
  outputs[CLOCK_OUTPUT].setVoltage(SNH.getClock());

  lights[BLINK1_LIGHT].setBrightness(lfo1.getBlink() < 0.5f ? 1.f : 0.f);
  lights[BLINK2_LIGHT].setBrightness(lfo2.getBlink() < 0.5f ? 1.f : 0.f);
  // The clock light follows the actual clock output, so it stays meaningful at
  // the multiplied/divided rate instead of the now-unused free-run phase.
  lights[CLOCK_LIGHT].setBrightness(SNH.getClock() > 5.f ? 1.f : 0.f);
}

KI1H_LFOWidget::KI1H_LFOWidget(KI1H_LFO *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-LFO.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);
  // ============================================================================
  // BLINKEN LIGHTS
  // ============================================================================
  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(COLUMNS[2], ROWS[3] - HALF_R)),
                                                      module, KI1H_LFO::BLINK1_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(COLUMNS[2], ROWS[5] - HALF_R)),
                                                      module, KI1H_LFO::BLINK2_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[1] - HALF_R)), module, KI1H_LFO::CLOCK_LIGHT));

  // ============================================================================
  // LFO 1 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<KI1HKnob>(mm2px(Vec(COLUMNS[0], ROWS[2])), module,
                                               KI1H_LFO::RATE1CV_PARAM));
  addParam(createParamCentered<KI1HBigKnob>(mm2px(Vec(COLUMNS[1], ROWS[3] - HALF_R)), module,
                                                  KI1H_LFO::RATE1_PARAM));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[3])), module,
                                             KI1H_LFO::CV1_INPUT));
  addParam(createParamCentered<KI1HSwitch>(mm2px(Vec(COLUMNS[2], ROWS[2])), module,
                                             KI1H_LFO::WAVE1_PARAM));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[2], ROWS[3])), module,
                                              KI1H_LFO::WAVE1_OUTPUT));

  // ============================================================================
  // LFO 2 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<KI1HKnob>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                               KI1H_LFO::RATE2CV_PARAM));
  addParam(createParamCentered<KI1HBigKnob>(mm2px(Vec(COLUMNS[1], ROWS[5] - HALF_R)), module,
                                                  KI1H_LFO::RATE2_PARAM));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                             KI1H_LFO::CV2_INPUT));
  addParam(createParamCentered<KI1HSwitch>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                             KI1H_LFO::WAVE2_PARAM));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                              KI1H_LFO::WAVE2_OUTPUT));

  // ============================================================================
  // S&H - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<KI1HKnob>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                               KI1H_LFO::SRATE_PARAM));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                              KI1H_LFO::CLOCK_INPUT));
  addParam(createParamCentered<KI1HKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_LFO::SLAG_PARAM));
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[1])), module, KI1H_LFO::SAMP_INPUT));
  addParam(createParamCentered<KI1HSwitch>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[1] - HALF_R)),
                                             module, KI1H_LFO::SWAVE_PARAM));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                              KI1H_LFO::SWAVE_OUTPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                             KI1H_LFO::CLOCK_OUTPUT));
}

Model *modelKI1H_LFO = createModel<KI1H_LFO, KI1H_LFOWidget>("KI1H-LFO");
