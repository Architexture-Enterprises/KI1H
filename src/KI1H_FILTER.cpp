#include "plugin.hpp"
#include <cmath>

// Float pi, so the coefficient expressions below stay in single precision
// instead of promoting through the double overloads of exp/cos/sin.
static constexpr float PI_F = 3.14159265358979323846f;

// ============================================================================
// CLASS DEFINITION
// ============================================================================
struct Filter {
  float getOutput() const {
    return output;
  }
  float output = 0.f;
};

struct LPFilter : Filter {
  void process(float input, float cutoff, float resonance, float sampletime);
  /** Restores exactly the state a freshly constructed LPFilter has. */
  void reset() {
    output = 0.f;
    cutoff_coeff = 0.f;
    cachedCutoff = -1.f;
    cachedSampletime = -1.f;
    for (int i = 0; i < 12; i++)
      stages[i] = 0.f;
  }
  static constexpr float minFreq = 20.f;
  static constexpr float maxFreq = 22000.f;
  float stages[12] = {};
  float cutoff_coeff = 0.f;

  // Cache keyed on the inputs the coefficient derives from. Negative
  // sentinels so the first process() call always computes.
  float cachedCutoff = -1.f;
  float cachedSampletime = -1.f;
};

struct BPFilter : Filter {
  void process(float input, float frequency, float width, float resonance, float sampletime);
  static constexpr float minFreq = 30.f;
  static constexpr float maxFreq = 15000.f;
  void setCoefficients(float w, float q) {
    float cos_w = std::cos(w);
    float sin_w = std::sin(w);
    float alpha = sin_w / (2.0f * q);

    float a0 = 1.0f + alpha;
    b0 = (1.0f - cos_w) / (2.0f * a0);
    b1 = (1.0f - cos_w) / a0;
    b2 = b0;
    a1 = (-2.0f * cos_w) / a0;
    a2 = (1.0f - alpha) / a0;
  }
  /** Restores exactly the state a freshly constructed BPFilter has. */
  void reset() {
    output = 0.f;
    hp_prev_in = hp_prev_out = 1.f;
    x1 = x2 = y1 = y2 = 0.f;
    b0 = b1 = b2 = a1 = a2 = 0.f;
    hp_alpha = 0.f;
    cachedFreq = -1.f;
    cachedWidth = -1.f;
    cachedRes = -1.f;
    cachedSampletime = -1.f;
  }

  // 6dB HP state
  float hp_prev_in = 1.f;
  float hp_prev_out = 1.f;
  float hp_alpha = 0.f;

  // Cache keyed on everything the coefficients derive from.
  float cachedFreq = -1.f;
  float cachedWidth = -1.f;
  float cachedRes = -1.f;
  float cachedSampletime = -1.f;

  // 12dB LP biquad states
  float x1 = 0.f, x2 = 0.f, y1 = 0.f, y2 = 0.f;             // State variables
  float b0 = 0.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;   // Coefficients
};

struct HPFilter : Filter {
  void process(float input, float cutoff, float sampletime);
  /** Restores exactly the state a freshly constructed HPFilter has. */
  void reset() {
    output = 0.f;
    prev_input = prev_output = 1.f;
    alpha = 0.f;
    cachedCutoff = -1.f;
    cachedSampletime = -1.f;
  }
  static constexpr float minFreq = 30.f;
  static constexpr float maxFreq = 10000.f;
  float prev_input = 1.f;
  float prev_output = 1.f;

  float alpha = 0.f;
  float cachedCutoff = -1.f;
  float cachedSampletime = -1.f;
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_FILTER : Module {
  enum ParamIds {
    LPFREQ_PARAM,
    BPFREQ1_PARAM,
    BPFREQ2_PARAM,
    HPFREQ_PARAM,
    LPMOD_PARAM,
    BPMOD1_PARAM,
    BPMOD2_PARAM,
    HPMOD_PARAM,
    LPRES_PARAM,
    BPRES1_PARAM,
    BPRES2_PARAM,
    BPWIDTH1_PARAM,
    BPWIDTH2_PARAM,
    FILT1LINK_PARAM,
    FILT2LINK_PARAM,
    BIGKNOB_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    LP_INPUT,
    BP1_INPUT,
    BP2_INPUT,
    HP_INPUT,
    LPMOD_INPUT,
    BPMOD1_INPUT,
    BPWIDTH1_INPUT,
    BPWIDTH2_INPUT,
    BPMOD2_INPUT,
    HPMOD_INPUT,
    BIGKNOB_INPUT,
    NUM_INPUTS
  };
  enum OutputIds { LP_OUTPUT, BP1_OUTPUT, BP2_OUTPUT, HP_OUTPUT, NUM_OUTPUTS };

  KI1H_FILTER();
  void process(const ProcessArgs &args) override;

  void onReset(const ResetEvent &e) override {
    Module::onReset(e);
    lpfilter.reset();
    bpfilter1.reset();
    bpfilter2.reset();
    hpfilter.reset();
  }

private:
  LPFilter lpfilter;
  BPFilter bpfilter1, bpfilter2;
  HPFilter hpfilter;
};

// ============================================================================
// WIDGET DEFINITION
// ============================================================================
struct KI1H_FILTERWidget : ModuleWidget {
  KI1H_FILTERWidget(KI1H_FILTER *module);
};

// ============================================================================
// PROCESS METHOD
// ============================================================================
void LPFilter::process(float input, float cutoff, float resonance, float sampletime) {
  // cutoff comes from a knob plus optional CV, so it is control rate. Only
  // pay for the exp() when it actually moves.
  if (cutoff != cachedCutoff || sampletime != cachedSampletime) {
    cachedCutoff = cutoff;
    cachedSampletime = sampletime;
    cutoff_coeff = 1.0f - std::exp(-2.0f * PI_F * cutoff * sampletime);
  }

  // Single feedback calculation
  float feedback = stages[11] * resonance;
  float signal = input - feedback;

  // Cascade of 12 one-pole lowpasses. Left as a loop and let -O3 unroll it.
  for (int i = 0; i < 12; i++) {
    float x = signal;
    if (i > 0)
      x = stages[i - 1];
    stages[i] += cutoff_coeff * (x - stages[i]);
  }
  output = stages[11];
}

void HPFilter::process(float input, float cutoff, float sampletime) {

  // High-pass coefficient, recomputed only when cutoff or sample rate moves.
  if (cutoff != cachedCutoff || sampletime != cachedSampletime) {
    cachedCutoff = cutoff;
    cachedSampletime = sampletime;
    alpha = std::exp(-2.0f * PI_F * cutoff * sampletime);
  }

  // RC high-pass
  float hp_out = alpha * (prev_output + input - prev_input);

  prev_input = input;
  prev_output = hp_out;

  output = hp_out;
}

void BPFilter::process(float input, float frequency, float width, float resonance,
                       float sampletime) {
  // frequency, width and resonance are all knob-plus-CV, i.e. control rate.
  // Recompute the coefficient set only when one of them actually moves; the
  // sample loop below runs every sample regardless.
  if (frequency != cachedFreq || width != cachedWidth || resonance != cachedRes ||
      sampletime != cachedSampletime) {
    cachedFreq = frequency;
    cachedWidth = width;
    cachedRes = resonance;
    cachedSampletime = sampletime;

    float bw = frequency * width;
    float q = (frequency / bw) * (1.f + resonance * 10.f);
    float hpFreq = frequency - bw / 2;
    float lpFreq = (bw / 2) + frequency;

    hpFreq = std::max(hpFreq, 30.f);
    lpFreq = std::min(15000.f, lpFreq);
    hp_alpha = std::exp(-2.0f * PI_F * hpFreq * sampletime);
    float w = 2.0f * PI_F * lpFreq * sampletime;
    setCoefficients(w, q);
  }

  float hp_out = hp_alpha * (hp_prev_out + input - hp_prev_in);
  hp_prev_in = input;
  hp_prev_out = hp_out;

  output = b0 * hp_out + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

  x2 = x1;
  x1 = hp_out;
  y2 = y1;
  y1 = output;
}

// ============================================================================
// CV MODULATION HELPERS
// ============================================================================
/** Adds a mod input's contribution to a cutoff frequency, in Hz per volt, and
re-clamps to the filter's range. Returns base unchanged when nothing is
patched.

Takes Input by non-const reference because Rack does not const-qualify
Input::isConnected() or Input::getVoltage(). */
static float applyFreqMod(Input &in, float base, float minFreq, float maxFreq) {
  if (!in.isConnected())
    return base;
  return clamp(base + in.getVoltage() * 1000.f, minFreq, maxFreq);
}

/** Scales a bandwidth by a bipolar mod input mapped from +/-5 V onto 0..1. */
static float applyWidthMod(Input &in, float width) {
  if (!in.isConnected())
    return width;
  return width * (in.getVoltage() + 5.f) / 10.f;
}

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_FILTER::KI1H_FILTER() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
  // ============================================================================
  // LP FILTER
  // ============================================================================
  configParam(KI1H_FILTER::LPFREQ_PARAM, LPFilter::minFreq, LPFilter::maxFreq,
              1000.f, "LP Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::LPRES_PARAM, 0.f, 1.666f, 0.f, "LP Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::LP_INPUT, "LP In");
  configInput(KI1H_FILTER::LPMOD_INPUT, "LP FM");
  configOutput(KI1H_FILTER::LP_OUTPUT, "LP Out");

  // ============================================================================
  // BP FILTERS
  // ============================================================================
  configParam(KI1H_FILTER::BPFREQ1_PARAM, BPFilter::minFreq, BPFilter::maxFreq,
              1000.f, "BP1 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWIDTH1_PARAM, 0.5f, 5.f, 1.f, "BP1 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRES1_PARAM, 0.01f, 1.666f, 0.01f, "BP1 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP1_INPUT, "BP1 In");
  configInput(KI1H_FILTER::BPMOD1_INPUT, "BP1 FM");
  configInput(KI1H_FILTER::BPWIDTH1_INPUT, "BP1 Width");
  configOutput(KI1H_FILTER::BP1_OUTPUT, "BP1 Out");

  configParam(KI1H_FILTER::BPFREQ2_PARAM, BPFilter::minFreq, BPFilter::maxFreq,
              1000.f, "BP2 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWIDTH2_PARAM, 0.5f, 5.f, 1.f, "BP2 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRES2_PARAM, 0.01f, 1.666f, 0.01f, "BP2 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP2_INPUT, "BP2 In");
  configInput(KI1H_FILTER::BPMOD2_INPUT, "BP2 FM");
  configInput(KI1H_FILTER::BPWIDTH2_INPUT, "BP2 Width");
  configOutput(KI1H_FILTER::BP2_OUTPUT, "BP2 Out");

  // ============================================================================
  // HP FILTER
  // ============================================================================
  configParam(KI1H_FILTER::HPFREQ_PARAM, HPFilter::minFreq, HPFilter::maxFreq,
              30.f, "HP Freq", " Hz", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::HP_INPUT, "HP In");
  configInput(KI1H_FILTER::HPMOD_INPUT, "HP FM");
  configOutput(KI1H_FILTER::HP_OUTPUT, "HP Out");

  // ============================================================================
  // LINKED CONTROLS
  // ============================================================================
  configParam(KI1H_FILTER::BIGKNOB_PARAM, 0.f, 1.f, 0.f, "Frequency", " Hz", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BIGKNOB_INPUT, "Linked Frequency");
  auto filter1link = configSwitch(FILT1LINK_PARAM, 0.f, 1.f, 0.f, "Filter 1 Link", {"on", "off"});
  filter1link->snapEnabled = true;
  auto filter2link = configSwitch(FILT2LINK_PARAM, 0.f, 1.f, 0.f, "Filter 2 Link", {"off", "on"});
  filter2link->snapEnabled = true;
}

// ============================================================================
// Filter - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_FILTER::process(const ProcessArgs &args) {
  float lpInput = inputs[LP_INPUT].getVoltage();
  float lpRes = params[LPRES_PARAM].getValue();
  float lpFreq = params[LPFREQ_PARAM].getValue();
  float bp1Freq = params[BPFREQ1_PARAM].getValue();
  float bp1Input = inputs[BP1_INPUT].getVoltage();
  float bp1Width = params[BPWIDTH1_PARAM].getValue();
  float bp1Res = params[BPRES1_PARAM].getValue();
  float bp2Input = inputs[BP2_INPUT].getVoltage();
  float bp2Freq = params[BPFREQ2_PARAM].getValue();
  float bp2Width = params[BPWIDTH2_PARAM].getValue();
  float bp2Res = params[BPRES2_PARAM].getValue();
  float hpInput = inputs[HP_INPUT].getVoltage();
  float hpFreq = params[HPFREQ_PARAM].getValue();
  float bigF = params[BIGKNOB_PARAM].getValue() * bpfilter1.maxFreq;
  int link1 = (int)params[FILT1LINK_PARAM].getValue();
  int link2 = (int)params[FILT2LINK_PARAM].getValue();

  lpFreq = applyFreqMod(inputs[LPMOD_INPUT], lpFreq, lpfilter.minFreq, lpfilter.maxFreq);
  bp1Freq = applyFreqMod(inputs[BPMOD1_INPUT], bp1Freq, bpfilter1.minFreq, bpfilter1.maxFreq);
  bp2Freq = applyFreqMod(inputs[BPMOD2_INPUT], bp2Freq, bpfilter2.minFreq, bpfilter2.maxFreq);
  hpFreq = applyFreqMod(inputs[HPMOD_INPUT], hpFreq, hpfilter.minFreq, hpfilter.maxFreq);
  bigF = applyFreqMod(inputs[BIGKNOB_INPUT], bigF, 0.f, bpfilter1.maxFreq);

  bp1Width = applyWidthMod(inputs[BPWIDTH1_INPUT], bp1Width);
  bp2Width = applyWidthMod(inputs[BPWIDTH2_INPUT], bp2Width);
  if (link1 == 0) {
    bp1Freq = clamp(bp1Freq + bigF, bpfilter1.minFreq, bpfilter1.maxFreq);
    lpFreq = clamp(lpFreq + bigF, lpfilter.minFreq, lpfilter.maxFreq);
  }
  if (link2 == 1) {
    hpFreq = clamp(hpFreq + bigF, hpfilter.minFreq, hpfilter.maxFreq);
    bp2Freq = clamp(bp2Freq + bigF, bpfilter2.minFreq, bpfilter2.maxFreq);
  }

  // Skip a filter whose result nobody can observe. BP1 and HP have to stay
  // live when their own jack is empty but the filter they normal into is
  // patched, otherwise the internal chain goes silent.
  const bool bp1Patched = outputs[BP1_OUTPUT].isConnected();
  const bool lpPatched = outputs[LP_OUTPUT].isConnected();
  const bool hpPatched = outputs[HP_OUTPUT].isConnected();
  const bool bp2Patched = outputs[BP2_OUTPUT].isConnected();

  if (bp1Patched || lpPatched)
    bpfilter1.process(bp1Input, bp1Freq, bp1Width, bp1Res, args.sampleTime);
  if (lpPatched) {
    if (!bp1Patched && !inputs[LP_INPUT].isConnected())
      lpInput = bpfilter1.getOutput();
    lpfilter.process(lpInput, lpFreq, lpRes, args.sampleTime);
  }

  if (hpPatched || bp2Patched)
    hpfilter.process(hpInput, hpFreq, args.sampleTime);
  if (bp2Patched) {
    if (!hpPatched && !inputs[BP2_INPUT].isConnected())
      bp2Input = hpfilter.getOutput();
    bpfilter2.process(bp2Input, bp2Freq, bp2Width, bp2Res, args.sampleTime);
  }

  outputs[LP_OUTPUT].setVoltage(lpfilter.getOutput());
  outputs[HP_OUTPUT].setVoltage(hpfilter.getOutput());
  outputs[BP1_OUTPUT].setVoltage(bpfilter1.getOutput() * 2.f);
  outputs[BP2_OUTPUT].setVoltage(bpfilter2.getOutput() * 2.f);
}

KI1H_FILTERWidget::KI1H_FILTERWidget(KI1H_FILTER *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-FILTER.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);

  // ============================================================================
  // LP SECTION
  // ============================================================================
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[3], ROWS[0])), module,
                                             KI1H_FILTER::LPMOD_INPUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[2])), module,
                                           KI1H_FILTER::LP_INPUT));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[3], ROWS[1])), module,
                                               KI1H_FILTER::LPFREQ_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[4], ROWS[1])), module,
                                               KI1H_FILTER::LPRES_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[0])), module,
                                             KI1H_FILTER::LP_OUTPUT));

  // ============================================================================
  // BP SECTION
  // ============================================================================
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[0])), module, KI1H_FILTER::BP1_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                             KI1H_FILTER::BPMOD1_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                             KI1H_FILTER::BPWIDTH1_INPUT));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                               KI1H_FILTER::BPFREQ1_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                               KI1H_FILTER::BPWIDTH1_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                               KI1H_FILTER::BPRES1_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[2])), module,
                                             KI1H_FILTER::BP1_OUTPUT));

  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[3])), module,
                                           KI1H_FILTER::BP2_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[3], ROWS[5])), module,
                                             KI1H_FILTER::BPMOD2_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                             KI1H_FILTER::BPWIDTH2_INPUT));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[3], ROWS[4])), module,
                                               KI1H_FILTER::BPFREQ2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                               KI1H_FILTER::BPWIDTH2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[4], ROWS[4])), module,
                                               KI1H_FILTER::BPRES2_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[5])), module,
                                             KI1H_FILTER::BP2_OUTPUT));

  // ============================================================================
  // HP SECTION
  // ============================================================================
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module, KI1H_FILTER::HP_INPUT));
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                             KI1H_FILTER::HPMOD_INPUT));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[4])), module,
                                               KI1H_FILTER::HPFREQ_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[3])), module,
                                             KI1H_FILTER::HP_OUTPUT));

  // ============================================================================
  // JOINT CONTROLS
  // ============================================================================
  addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                             KI1H_FILTER::BIGKNOB_INPUT));
  addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[3] - HALF_R)), module,
                                                  KI1H_FILTER::BIGKNOB_PARAM));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[2] - HALF_R)),
                                             module, KI1H_FILTER::FILT1LINK_PARAM));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[3] - HALF_C, ROWS[4] - HALF_R)),
                                             module, KI1H_FILTER::FILT2LINK_PARAM));
}

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
