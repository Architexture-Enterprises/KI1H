
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
  float getOutput() {
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
  enum PARAM_IDS {
    LPFreq,
    BPFreq1,
    BPFreq2,
    HPFreq,
    LPMod,
    BPMod1,
    BPMod2,
    HPMod,
    LPRes,
    BPRes1,
    BPRes2,
    BPWidth1,
    BPWidth2,
    Filt1Link,
    Filt2Link,
    BigKnob,
    NUM_PARAMS
  };
  enum INPUT_IDS {
    LPIN,
    BP1IN,
    BP2IN,
    HPIN,
    LPMOD_IN,
    BPMOD1_IN,
    BPWIDTH1_IN,
    BPWIDTH2_IN,
    BPMOD2_IN,
    HPMOD_IN,
    BIGKNOB_IN,
    NUM_INPUTS
  };
  enum OUTPUT_IDS { LPOUT, BPOUT1, BPOUT2, HPOUT, NUM_OUTPUTS };

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
  configParam(KI1H_FILTER::LPFreq, KI1H_FILTER::lpfilter.minFreq, KI1H_FILTER::lpfilter.maxFreq,
              0.1f, "LP Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::LPRes, 0.f, 1.666f, 0.f, "LP Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::LPIN, "LP In");
  configInput(KI1H_FILTER::LPMOD_IN, "LP FM");
  configOutput(KI1H_FILTER::LPOUT, "LP Out");

  // ============================================================================
  // BP FILTERS
  // ============================================================================
  configParam(KI1H_FILTER::BPFreq1, KI1H_FILTER::bpfilter1.minFreq, KI1H_FILTER::bpfilter1.maxFreq,
              0.1f, "BP1 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWidth1, 0.5, 5.f, 0.f, "BP1 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRes1, 0.01, 1.666f, 0.f, "BP1 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP1IN, "BP1 In");
  configInput(KI1H_FILTER::BPMOD1_IN, "BP1 FM");
  configInput(KI1H_FILTER::BPWIDTH1_IN, "BP1 Width");
  configOutput(KI1H_FILTER::BPOUT1, "BP1 Out");

  configParam(KI1H_FILTER::BPFreq2, KI1H_FILTER::bpfilter2.minFreq, KI1H_FILTER::bpfilter2.maxFreq,
              0.1f, "BP2 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWidth2, 0.5f, 5.f, 0.f, "BP2 Width", " %", 0.f, 20.f, 0.f);
  configParam(KI1H_FILTER::BPRes2, 0.01f, 1.666f, 0.f, "BP2 Resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP2IN, "BP2 In");
  configInput(KI1H_FILTER::BPMOD2_IN, "BP2 FM");
  configInput(KI1H_FILTER::BPWIDTH2_IN, "BP2 Width");
  configOutput(KI1H_FILTER::BPOUT2, "BP2 Out");

  // ============================================================================
  // HP FILTER
  // ============================================================================
  configParam(KI1H_FILTER::HPFreq, KI1H_FILTER::hpfilter.minFreq, KI1H_FILTER::hpfilter.maxFreq,
              1.f, "HP Freq", " Hz", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::HPIN, "HP In");
  configInput(KI1H_FILTER::HPMOD_IN, "HP FM");
  configOutput(KI1H_FILTER::HPOUT, "HP Out");

  // ============================================================================
  // LINKED CONTROLS
  // ============================================================================
  configParam(KI1H_FILTER::BigKnob, 0.f, 1.f, 0.f, "Frequency", " Hz", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BIGKNOB_IN, "Linked Frequency");
  auto filter1link = configSwitch(Filt1Link, 0.f, 1.f, 0.f, "Filter 1 Link", {"on", "off"});
  filter1link->snapEnabled = true;
  auto filter2link = configSwitch(Filt2Link, 0.f, 1.f, 0.f, "Filter 2 Link", {"off", "on"});
  filter2link->snapEnabled = true;
};

// ============================================================================
// Filter - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_FILTER::process(const ProcessArgs &args) {
  float lpInput = inputs[LPIN].getVoltage();
  float lpRes = params[LPRes].getValue();
  float lpFreq = params[LPFreq].getValue();
  float bp1Freq = params[BPFreq1].getValue();
  float bp1Input = inputs[BP1IN].getVoltage();
  float bp1Width = params[BPWidth1].getValue();
  float bp1Res = params[BPRes1].getValue();
  float bp2Input = inputs[BP2IN].getVoltage();
  float bp2Freq = params[BPFreq2].getValue();
  float bp2Width = params[BPWidth2].getValue();
  float bp2Res = params[BPRes2].getValue();
  float hpInput = inputs[HPIN].getVoltage();
  float hpFreq = params[HPFreq].getValue();
  float bigF = params[BigKnob].getValue() * bpfilter1.maxFreq;
  float link1 = params[Filt1Link].getValue();
  float link2 = params[Filt2Link].getValue();

  float lpMod = inputs[LPMOD_IN].getVoltage();
  float bp1Mod = inputs[BPMOD1_IN].getVoltage();
  float bp1WidthMod = inputs[BPWIDTH1_IN].getVoltage();
  float bp2Mod = inputs[BPMOD2_IN].getVoltage();
  float bp2WidthMod = inputs[BPWIDTH2_IN].getVoltage();
  float hpMod = inputs[HPMOD_IN].getVoltage();
  float bigKnobMod = inputs[BIGKNOB_IN].getVoltage();

  if (inputs[LPMOD_IN].isConnected()) {
    lpFreq += lpMod * 1000.f;
    lpFreq = clamp(lpFreq, lpfilter.minFreq, lpfilter.maxFreq);
  }

  if (inputs[BPMOD1_IN].isConnected()) {
    bp1Freq += bp1Mod * 1000.f;
    bp1Freq = clamp(bp1Freq, bpfilter1.minFreq, bpfilter1.maxFreq);
  }

  if (inputs[BPWIDTH1_IN].isConnected())
    bp1Width *= (bp1WidthMod + 5.f) / 10.f;

  if (inputs[BPMOD2_IN].isConnected()) {
    bp2Freq += bp2Mod * 1000.f;
    bp2Freq = clamp(bp2Freq, bpfilter2.minFreq, bpfilter2.maxFreq);
  }

  if (inputs[BPWIDTH2_IN].isConnected())
    bp2Width *= (bp2WidthMod + 5.f) / 10.f;

  if (inputs[HPMOD_IN].isConnected()) {
    hpFreq += hpMod * 1000.f;
    hpFreq = clamp(hpFreq, hpfilter.minFreq, hpfilter.maxFreq);
  }

  if (inputs[BIGKNOB_IN].isConnected()) {
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
  if (!outputs[BPOUT1].isConnected() && !inputs[LPIN].isConnected())
    lpInput = bpfilter1.getOutput();
  lpfilter.process(lpInput, lpFreq, lpRes, args.sampleTime);

  hpfilter.process(hpInput, hpFreq, args.sampleTime);
  if (!outputs[HPOUT].isConnected() && !inputs[BP2IN].isConnected())
    bp2Input = hpfilter.getOutput();
  bpfilter2.process(bp2Input, bp2Freq, bp2Width, bp2Res, args.sampleTime);

  outputs[LPOUT].setVoltage(lpfilter.getOutput());
  outputs[HPOUT].setVoltage(hpfilter.getOutput());
  outputs[BPOUT1].setVoltage(bpfilter1.getOutput() * 2.f);
  outputs[BPOUT2].setVoltage(bpfilter2.getOutput() * 2.f);
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
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3], ROWS[0])), module,
                                           KI1H_FILTER::LPMOD_IN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[2])), module,
                                           KI1H_FILTER::LPIN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[3], ROWS[1])), module,
                                               KI1H_FILTER::LPFreq));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[4], ROWS[1])), module,
                                               KI1H_FILTER::LPRes));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[0])), module,
                                             KI1H_FILTER::LPOUT));

  // ============================================================================
  // BP SECTION
  // ============================================================================
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[0])), module, KI1H_FILTER::BP1IN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                           KI1H_FILTER::BPMOD1_IN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                           KI1H_FILTER::BPWIDTH1_IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                               KI1H_FILTER::BPFreq1));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                               KI1H_FILTER::BPWidth1));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                               KI1H_FILTER::BPRes1));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[2])), module,
                                             KI1H_FILTER::BPOUT1));

  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[3])), module,
                                           KI1H_FILTER::BP2IN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3], ROWS[5])), module,
                                           KI1H_FILTER::BPMOD2_IN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                           KI1H_FILTER::BPWIDTH2_IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[3], ROWS[4])), module,
                                               KI1H_FILTER::BPFreq2));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                               KI1H_FILTER::BPWidth2));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[4], ROWS[4])), module,
                                               KI1H_FILTER::BPRes2));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[5])), module,
                                             KI1H_FILTER::BPOUT2));

  // ============================================================================
  // HP SECTION
  // ============================================================================
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module, KI1H_FILTER::HPIN));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                           KI1H_FILTER::HPMOD_IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[4])), module,
                                               KI1H_FILTER::HPFreq));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[3])), module,
                                             KI1H_FILTER::HPOUT));

  // ============================================================================
  // JOINT CONTROLS
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                           KI1H_FILTER::BIGKNOB_IN));
  addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[3] - HALF_R)), module,
                                                  KI1H_FILTER::BigKnob));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[2] - HALF_R)),
                                             module, KI1H_FILTER::Filt1Link));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[3] - HALF_C, ROWS[4] - HALF_R)),
                                             module, KI1H_FILTER::Filt2Link));
};

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
