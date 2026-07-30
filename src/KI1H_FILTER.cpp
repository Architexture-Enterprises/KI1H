
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "plugin.hpp"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

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
  float minFreq = 20.f;
  float maxFreq = 22000.f;
  float stages[12];
  float cutoff_coeff;
};

struct BPFilter : Filter {
  void process(float input, float frequency, float width, float resonance, float sampletime);
  float minFreq = 30.f;
  float maxFreq = 15000.f;
  void setCoefficients(float w, float q) {
    float cos_w = cos(w);
    float sin_w = sin(w);
    float alpha = sin_w / (2.0f * q);

    float a0 = 1.0f + alpha;
    b0 = (1.0f - cos_w) / (2.0f * a0);
    b1 = (1.0f - cos_w) / a0;
    b2 = b0;
    a1 = (-2.0f * cos_w) / a0;
    a2 = (1.0f - alpha) / a0;
  }
  // 6dB HP state
  float hp_prev_in = 1.f;
  float hp_prev_out = 1.f;

  // 12dB LP biquad states
  float x1, x2, y1, y2;     // State variables
  float b0, b1, b2, a1, a2; // Coefficients
};

struct HPFilter : Filter {
  void process(float input, float cutoff, float sampletime);
  float minFreq = 30.f;
  float maxFreq = 10000.f;
  float prev_input = 1.f;
  float prev_output = 1.f;
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
  // Pre-calculate coefficient once per sample
  cutoff_coeff = 1.0f - exp(-2.0f * M_PI * cutoff * sampletime);

  // Single feedback calculation
  float feedback = stages[11] * resonance;
  float signal = input - feedback;

  for (int i = 0; i < 12; i++) {
    float x = signal;
    if (i > 0)
      x = stages[i - 1];
    // 12 simple one-poles (unrolled for efficiency)
    stages[i] += cutoff_coeff * (x - stages[i]);
  }
  output = stages[11];
}

void HPFilter::process(float input, float cutoff, float sampletime) {

  // High-pass coefficient
  float alpha = exp(-2.0f * M_PI * cutoff * sampletime);

  // RC high-pass
  float hp_out = alpha * (prev_output + input - prev_input);

  prev_input = input;
  prev_output = hp_out;

  output = hp_out;
};

void BPFilter::process(float input, float frequency, float width, float resonance,
                       float sampletime) {
  float bw = frequency * width;
  float q = (frequency / bw) * (1.f + resonance * 10.f);
  float hpFreq = frequency - bw / 2;
  float lpFreq = (bw / 2) + frequency;

  hpFreq = std::max(hpFreq, 30.f);
  lpFreq = std::min(15000.f, lpFreq);
  float hp_alpha = exp(-2.0f * M_PI * hpFreq * sampletime);
  float w = 2.0f * M_PI * lpFreq * sampletime;
  setCoefficients(w, q);
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
// MODULE CONFIGURATION
// ============================================================================
KI1H_FILTER::KI1H_FILTER() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
  // ============================================================================
  // LP FILTER
  // ============================================================================
  configParam(KI1H_FILTER::LPFREQ_PARAM, KI1H_FILTER::lpfilter.minFreq, KI1H_FILTER::lpfilter.maxFreq,
              0.1f, "LP Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::LPRES_PARAM, 0.f, 1.666f, 0.f, "LP Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::LP_INPUT, "LP In");
  configInput(KI1H_FILTER::LPMOD_INPUT, "LP FM");
  configOutput(KI1H_FILTER::LP_OUTPUT, "LP Out");

  // ============================================================================
  // BP FILTERS
  // ============================================================================
  configParam(KI1H_FILTER::BPFREQ1_PARAM, KI1H_FILTER::bpfilter1.minFreq, KI1H_FILTER::bpfilter1.maxFreq,
              0.1f, "BP1 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWIDTH1_PARAM, 0.5, 5.f, 0.f, "BP1 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRES1_PARAM, 0.01, 1.666f, 0.f, "BP1 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP1_INPUT, "BP1 In");
  configInput(KI1H_FILTER::BPMOD1_INPUT, "BP1 FM");
  configInput(KI1H_FILTER::BPWIDTH1_INPUT, "BP1 Width");
  configOutput(KI1H_FILTER::BP1_OUTPUT, "BP1 Out");

  configParam(KI1H_FILTER::BPFREQ2_PARAM, KI1H_FILTER::bpfilter2.minFreq, KI1H_FILTER::bpfilter2.maxFreq,
              0.1f, "BP2 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWIDTH2_PARAM, 0.5f, 5.f, 0.f, "BP2 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRES2_PARAM, 0.01f, 1.666f, 0.f, "BP2 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP2_INPUT, "BP2 In");
  configInput(KI1H_FILTER::BPMOD2_INPUT, "BP2 FM");
  configInput(KI1H_FILTER::BPWIDTH2_INPUT, "BP2 Width");
  configOutput(KI1H_FILTER::BP2_OUTPUT, "BP2 Out");

  // ============================================================================
  // HP FILTER
  // ============================================================================
  configParam(KI1H_FILTER::HPFREQ_PARAM, KI1H_FILTER::hpfilter.minFreq, KI1H_FILTER::hpfilter.maxFreq,
              1.f, "HP Freq", " Hz", 0.f, 1.f, 0.f);
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
};

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
  float link1 = params[FILT1LINK_PARAM].getValue();
  float link2 = params[FILT2LINK_PARAM].getValue();

  float lpMod = inputs[LPMOD_INPUT].getVoltage();
  float bp1Mod = inputs[BPMOD1_INPUT].getVoltage();
  float bp1WidthMod = inputs[BPWIDTH1_INPUT].getVoltage();
  float bp2Mod = inputs[BPMOD2_INPUT].getVoltage();
  float bp2WidthMod = inputs[BPWIDTH2_INPUT].getVoltage();
  float hpMod = inputs[HPMOD_INPUT].getVoltage();
  float bigKnobMod = inputs[BIGKNOB_INPUT].getVoltage();

  if (inputs[LPMOD_INPUT].isConnected()) {
    lpFreq += lpMod * 1000.f;
    lpFreq = clamp(lpFreq, lpfilter.minFreq, lpfilter.maxFreq);
  }

  if (inputs[BPMOD1_INPUT].isConnected()) {
    bp1Freq += bp1Mod * 1000.f;
    bp1Freq = clamp(bp1Freq, bpfilter1.minFreq, bpfilter1.maxFreq);
  }

  if (inputs[BPWIDTH1_INPUT].isConnected())
    bp1Width *= (bp1WidthMod + 5.f) / 10.f;

  if (inputs[BPMOD2_INPUT].isConnected()) {
    bp2Freq += bp2Mod * 1000.f;
    bp2Freq = clamp(bp2Freq, bpfilter2.minFreq, bpfilter2.maxFreq);
  }

  if (inputs[BPWIDTH2_INPUT].isConnected())
    bp2Width *= (bp2WidthMod + 5.f) / 10.f;

  if (inputs[HPMOD_INPUT].isConnected()) {
    hpFreq += hpMod * 1000.f;
    hpFreq = clamp(hpFreq, hpfilter.minFreq, hpfilter.maxFreq);
  }

  if (inputs[BIGKNOB_INPUT].isConnected()) {
    bigF += bigKnobMod * 1000.f;
    bigF = clamp(bigF, 0.f, bpfilter1.maxFreq);
  }
  if (link1 == 0.f) {
    bp1Freq = clamp(bp1Freq + bigF, bpfilter1.minFreq, bpfilter1.maxFreq);
    lpFreq = clamp(lpFreq + bigF, lpfilter.minFreq, lpfilter.maxFreq);
  }
  if (link2 == 1.f) {
    hpFreq = clamp(hpFreq + bigF, hpfilter.minFreq, hpfilter.maxFreq);
    bp2Freq = clamp(bp2Freq + bigF, bpfilter2.minFreq, bpfilter2.maxFreq);
  }

  bpfilter1.process(bp1Input, bp1Freq, bp1Width, bp1Res, args.sampleTime);
  if (!outputs[BP1_OUTPUT].isConnected() && !inputs[LP_INPUT].isConnected())
    lpInput = bpfilter1.getOutput();
  lpfilter.process(lpInput, lpFreq, lpRes, args.sampleTime);

  hpfilter.process(hpInput, hpFreq, args.sampleTime);
  if (!outputs[HP_OUTPUT].isConnected() && !inputs[BP2_INPUT].isConnected())
    bp2Input = hpfilter.getOutput();
  bpfilter2.process(bp2Input, bp2Freq, bp2Width, bp2Res, args.sampleTime);

  outputs[LP_OUTPUT].setVoltage(lpfilter.getOutput());
  outputs[HP_OUTPUT].setVoltage(hpfilter.getOutput());
  outputs[BP1_OUTPUT].setVoltage(bpfilter1.getOutput() * 2.f);
  outputs[BP2_OUTPUT].setVoltage(bpfilter2.getOutput() * 2.f);
};

KI1H_FILTERWidget::KI1H_FILTERWidget(KI1H_FILTER *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-FILTER.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewBlack>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

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
};

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
