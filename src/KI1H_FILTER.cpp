
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "plugin.hpp"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// class EfficientTransistorLadder {
//   private:
//       float stages[12];
//       float cutoff_coeff;

//   public:
//       float process(float input, float cutoff, float resonance, float drive) {
//           // Pre-calculate coefficient once per sample
//           cutoff_coeff = 1.0f - exp(-2.0f * M_PI * cutoff / sampleRate);

//           // Single feedback calculation
//           float feedback = stages[11] * resonance;
//           float signal = input - feedback;

//           // Shared saturation for "transistor warmth"
//           signal = tanh(drive * signal);

//           // 12 simple one-poles (unrolled for efficiency)
//           stages[0] += cutoff_coeff * (signal - stages[0]);
//           stages[1] += cutoff_coeff * (stages[0] - stages[1]);
//           stages[2] += cutoff_coeff * (stages[1] - stages[2]);
//           // ... continue for all 12 stages

//           return stages[11];
//       }
//   };

// class MOSFETHighpass {
//   private:
//       float prev_input, prev_output;

//   public:
//       float process(float input, float cutoff, float drive) {
//           // High-pass coefficient
//           float alpha = exp(-2.0f * M_PI * cutoff / sampleRate);

//           // RC high-pass
//           float hp_out = alpha * (prev_output + input - prev_input);

//           // MOSFET-style soft clipping (cheaper than tanh)
//           float driven = hp_out * drive;
//           float clipped = driven / (1.0f + abs(driven));  // Soft clip approximation

//           prev_input = input;
//           prev_output = clipped;

//           return clipped;
//       }
//   };

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
// MODULAR FILTER SECTION CLASS (SUGGESTED DESIGN)
// ============================================================================

// class ModularFilterSection {
// private:
//     EfficientTransistorLadder lp_filter;      // For top section
//     MOSFETHighpass hp_filter;                 // For bottom section
//     EfficientBandpass bp_filter;              // For both sections
//
//     bool is_linked = false;
//     bool is_top_section = true;  // vs bottom section
//
// public:
//     void setLinkMode(bool linked) { is_linked = linked; }
//
//     std::pair<float, float> process(float input, float freq1, float freq2,
//                                   float res1, float res2, float drive) {
//         float output1, output2;
//
//         if (is_top_section) {
//             if (is_linked) {
//                 // LP → BP series
//                 float lp_out = lp_filter.process(input, freq1, res1, drive);
//                 output1 = bp_filter.process(lp_out, freq2, res2);
//                 output2 = 0.0f;  // Only one output in linked mode
//             } else {
//                 // LP and BP parallel
//                 output1 = lp_filter.process(input, freq1, res1, drive);
//                 output2 = bp_filter.process(input, freq2, res2);
//             }
//         } else {
//             if (is_linked) {
//                 // BP → HP series
//                 float bp_out = bp_filter.process(input, freq1, res1);
//                 output1 = hp_filter.process(bp_out, freq2, drive);
//                 output2 = 0.0f;  // Only one output in linked mode
//             } else {
//                 // BP and HP parallel
//                 output1 = bp_filter.process(input, freq1, res1);
//                 output2 = hp_filter.process(input, freq2, drive);
//             }
//         }
//
//         return {output1, output2};
//     }
// };

// ============================================================================
// FREQUENCY LINKER CLASS (SUGGESTED DESIGN)
// ============================================================================

// class FrequencyLinker {
// private:
//     float base_frequency = 1000.0f;
//     float frequency_ratio = 1.0f;  // For linked mode
//     bool sync_enabled = false;
//
// public:
//     std::pair<float, float> getLinkedFrequencies(float freq_control, float ratio_control) {
//         if (sync_enabled) {
//             float freq1 = freq_control;
//             float freq2 = freq_control * ratio_control;  // Ratio-based linking
//             return {freq1, freq2};
//         } else {
//             // Independent control - you'd get freq2 from another parameter
//             return {freq_control, second_freq_control};
//         }
//     }
//
//     void setSyncMode(bool enabled) { sync_enabled = enabled; }
// };

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
  void process(float input, float cutoff, float resonance, float drive);

private:
  float stages[12];
  float cutoff_coeff;
};
class BPFilter : public Filter {
public:
  void process(float input, float frequency, float width);

private:
  // 6dB HP state
  float hp_prev_in, hp_prev_out;

  // 12dB LP biquad states
  float lp_x1, lp_x2, lp_y1, lp_y2;
  float lp_b0, lp_b1, lp_b2, lp_a1, lp_a2;
};
class HPFilter : public Filter {
public:
  void process(float input, float cutoff, float sampletime);

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
    BPR1,
    BPR2,
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

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_FILTER::KI1H_FILTER() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  configParam(KI1H_FILTER::HPFreq, 30.f, 10000.f, 1000.f);
};

// ============================================================================
// Filter - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_FILTER::process(const ProcessArgs &args) {
  // float lpInput = inputs[LPIN].getVoltage();
  // float lpFreq = params[LPFreq].getValue();
  float hpInput = inputs[HPIN].getVoltage();
  float hpFreq = params[HPFreq].getValue();

  hpfilter.process(hpInput, hpFreq, args.sampleTime);
  outputs[HPOUT].setVoltage(hpfilter.getOutput());
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
  // HP SECTION
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 30)), module, KI1H_FILTER::HPIN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 45)), module, KI1H_FILTER::HPFreq));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(10, 60)), module, KI1H_FILTER::HPOUT));
};

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
