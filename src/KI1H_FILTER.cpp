
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "plugin.hpp"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

float calcFreq(float min, float max) {
  return max / min;
}
// class EfficientBandpass {
//   private:
//       // 6dB HP state
//       float hp_prev_in, hp_prev_out;

//       // 12dB LP biquad states
//       float lp_x1, lp_x2, lp_y1, lp_y2;
//       float lp_b0, lp_b1, lp_b2, lp_a1, lp_a2;

//   public:
//       void updateCoefficients(float hp_freq, float lp_freq, float q) {
//           // Pre-calculate coefficients (call only when parameters change)
//           hp_alpha = exp(-2.0f * M_PI * hp_freq / sampleRate);

//           // Biquad LP coefficients
//           float w = 2.0f * M_PI * lp_freq / sampleRate;
//           float cos_w = cos(w);
//           float sin_w = sin(w);
//           float alpha = sin_w / (2.0f * q);

//           float a0 = 1.0f + alpha;
//           lp_b0 = (1.0f - cos_w) / (2.0f * a0);
//           lp_b1 = (1.0f - cos_w) / a0;
//           lp_b2 = lp_b0;
//           lp_a1 = (-2.0f * cos_w) / a0;
//           lp_a2 = (1.0f - alpha) / a0;
//       }

//       float process(float input) {
//           // 6dB HP first
//           float hp_out = hp_alpha * (hp_prev_out + input - hp_prev_in);
//           hp_prev_in = input;
//           hp_prev_out = hp_out;

//           // 12dB LP second (biquad)
//           float lp_out = lp_b0*hp_out + lp_b1*lp_x1 + lp_b2*lp_x2 - lp_a1*lp_y1 - lp_a2*lp_y2;

//           // Update delays
//           lp_x2 = lp_x1; lp_x1 = hp_out;
//           lp_y2 = lp_y1; lp_y1 = lp_out;

//           return lp_out;
//       }
//   };

// class TwoPoleLP {
//   private:
//       float x1, x2, y1, y2;  // State variables
//       float b0, b1, b2, a1, a2;  // Coefficients

//   public:
//       TwoPoleLP() : x1(0), x2(0), y1(0), y2(0) {}

//       void setCoefficients(float frequency, float q, float sampleRate) {
//           float w = 2.0f * M_PI * frequency / sampleRate;
//           float cos_w = cos(w);
//           float sin_w = sin(w);
//           float alpha = sin_w / (2.0f * q);

//           float a0 = 1.0f + alpha;
//           b0 = (1.0f - cos_w) / (2.0f * a0);
//           b1 = (1.0f - cos_w) / a0;
//           b2 = b0;
//           a1 = (-2.0f * cos_w) / a0;
//           a2 = (1.0f - alpha) / a0;
//       }

//       float process(float input) {
//           float output = b0*input + b1*x1 + b2*x2 - a1*y1 - a2*y2;

//           x2 = x1; x1 = input;
//           y2 = y1; y1 = output;

//           return output;
//       }
//   };

// Only recalculate when parameters actually change
// if (freq != prev_freq || res != prev_res) {
//   updateCoefficients(freq, res);
//   prev_freq = freq; prev_res = res;
// }

// float fast_tanh(float x) {
//   float x2 = x * x;
//   return x * (27.0f + x2) / (27.0f + 9.0f * x2);  // Rational approximation
// }

// float fast_exp(float x) {
//   return 1.0f / (1.0f - x + 0.5f * x * x);  // For small x in filter coefficients
// }

// ============================================================================
// CLASS DEFINITION
// ============================================================================
class Filter {
public:
  float getOutput() {
    return output;
  }

protected:
  float output = 0.f;
};

class LPFilter : public Filter {
public:
  void process(float input, float cutoff, float resonance, float sampletime);
  float minFreq = 20.f;
  float maxFreq = 22000.f;

private:
  float stages[12];
  float cutoff_coeff;
};
class BPFilter : public Filter {
public:
  void process(float input, float frequency, float width, float sampletime);
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

private:
  // 6dB HP state
  float hp_prev_in = 1.f;
  float hp_prev_out = 1.f;

  // 12dB LP biquad states
  float x1, x2, y1, y2;     // State variables
  float b0, b1, b2, a1, a2; // Coefficients
};
class HPFilter : public Filter {
public:
  void process(float input, float cutoff, float sampletime);
  float minFreq = 30.f;
  float maxFreq = 10000.f;

private:
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
    LPRes,
    BPWidth1,
    BPWidth2,
    HPRes,
    Filter1Freq,
    Filter2Freq,
    AllFreq,
    NUM_PARAMS
  };
  enum INPUT_IDS {
    LPIN,
    BP1IN,
    BP2IN,
    HPIN,
    LPFREQ_IN,
    BPFREQ1_IN,
    BPFREQ2_IN,
    HPFREQ_IN,
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

  // Shared saturation for "transistor warmth"
  // signal = tanh(drive * signal);
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

  // MOSFET-style soft clipping (cheaper than tanh)
  // float driven = hp_out * drive;
  // float clipped = driven / (1.0f + abs(driven)); // Soft clip approximation

  prev_input = input;
  prev_output = hp_out;

  output = hp_out;
};

void BPFilter::process(float input, float frequency, float width, float sampletime) {
  float hp_alpha = exp(-2.0f * M_PI * frequency * sampletime);
  float w = 2.0f * M_PI * frequency * sampletime;
  setCoefficients(w, width);
  float hp_out = hp_alpha * (hp_prev_out + input - hp_prev_in);
  hp_prev_in = input;
  hp_prev_out = hp_out;

  output = b0 * hp_out + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

  x2 = x1;
  x1 = hp_out;
  y2 = y1;
  y1 = output;

  //           return output;
}

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_FILTER::KI1H_FILTER() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
  configParam(KI1H_FILTER::LPFreq, KI1H_FILTER::lpfilter.minFreq, KI1H_FILTER::lpfilter.maxFreq,
              0.1f, "LP Freq", " Hz",
              calcFreq(KI1H_FILTER::lpfilter.minFreq, KI1H_FILTER::lpfilter.maxFreq), 1.f, 0.f);
  configParam(KI1H_FILTER::LPRes, 0.f, 1.f, 0.f, "LP resonance", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::LPIN, "LP In");
  configOutput(KI1H_FILTER::LPOUT, "LP Out");

  configParam(KI1H_FILTER::BPFreq1, KI1H_FILTER::bpfilter1.minFreq, KI1H_FILTER::bpfilter1.maxFreq,
              0.1f, "BP1 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWidth1, 0.001f, 1.f, 0.f, "BP1 Width", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP1IN, "BP1 In");
  configOutput(KI1H_FILTER::BPOUT1, "BP1 Out");
  configParam(KI1H_FILTER::BPFreq2, KI1H_FILTER::bpfilter2.minFreq, KI1H_FILTER::bpfilter2.maxFreq,
              0.1f, "BP2 Freq", " Hz", 0.f, 1.f, 0.f);
  configParam(KI1H_FILTER::BPWidth2, 0.001f, 1.f, 0.f, "BP2 Width", " %", 0.f, 1.f, 0.f);
  configInput(KI1H_FILTER::BP2IN, "BP2 In");
  configOutput(KI1H_FILTER::BPOUT2, "BP2 Out");

  configParam(KI1H_FILTER::HPFreq, KI1H_FILTER::hpfilter.minFreq, KI1H_FILTER::hpfilter.maxFreq,
              1.f, "HP Freq", " Hz",
              calcFreq(KI1H_FILTER::hpfilter.minFreq, KI1H_FILTER::hpfilter.maxFreq), 1.f, 0.f);
  configInput(KI1H_FILTER::HPIN, "HP In");
  configOutput(KI1H_FILTER::HPOUT, "HP Out");
};

// ============================================================================
// Filter - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_FILTER::process(const ProcessArgs &args) {
  float lpInput = inputs[LPIN].getVoltage();
  float lpRes = params[LPRes].getValue();
  float lpFreq = params[LPFreq].getValue();
  float bp2Input = inputs[BP2IN].getVoltage();
  float bp2Freq = params[BPFreq2].getValue();
  float bp2Width = params[BPWidth2].getValue();
  float bp1Freq = params[BPFreq1].getValue();
  float bp1Input = inputs[BP2IN].getVoltage();
  float bp1Width = params[BPWidth1].getValue();
  float hpInput = inputs[HPIN].getVoltage();
  float hpFreq = params[HPFreq].getValue();
  // float lpCutoff = calcFreq(lpfilter.minFreq, lpfilter.maxFreq, lpFreq);
  // float hpCutoff = calcFreq(hpfilter.minFreq, hpfilter.maxFreq, hpFreq);
  lpfilter.process(lpInput, lpFreq, lpRes, args.sampleTime);
  bpfilter1.process(bp1Input, bp1Freq, bp1Width, args.sampleTime);
  bpfilter2.process(bp2Input, bp2Freq, bp2Width, args.sampleTime);
  hpfilter.process(hpInput, hpFreq, args.sampleTime);

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
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 45)), module, KI1H_FILTER::LPIN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 30)), module, KI1H_FILTER::LPFreq));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25, 30)), module, KI1H_FILTER::LPRes));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25, 45)), module, KI1H_FILTER::LPOUT));

  // ============================================================================
  // BP SECTION
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40, 45)), module, KI1H_FILTER::BP1IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(40, 30)), module, KI1H_FILTER::BPFreq1));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(55, 30)), module, KI1H_FILTER::BPWidth1));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(55, 45)), module, KI1H_FILTER::BPOUT1));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 80)), module, KI1H_FILTER::BP2IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 65)), module, KI1H_FILTER::BPFreq2));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(25, 65)), module, KI1H_FILTER::BPWidth2));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25, 80)), module, KI1H_FILTER::BPOUT2));

  // ============================================================================
  // HP SECTION
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55, 80)), module, KI1H_FILTER::HPIN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(55, 65)), module, KI1H_FILTER::HPFreq));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70, 80)), module, KI1H_FILTER::HPOUT));
};

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
