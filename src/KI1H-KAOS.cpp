#include "plugin.hpp"
#include <random>

struct KAOS {
public:
  void process(float color, float bkIn, float pkIn);
  float getNoise() const {
    return noise;
  }
  float getpKaos() const {
    return pKaosOut;
  }
  float getbKaos() const {
    return bKaosOut;
  }
  float noise = 1.f;
  float pKaosOut = 0.f;
  float bKaosOut = 0.f;
  dsp::SchmittTrigger pKaosTrigger;
  dsp::SchmittTrigger bKaosTrigger;

  // Brown noise state (integrator for 1/f² spectrum)
  float brownState = 0.f;

  // Pink noise state variables (Paul Kellet's algorithm)
  float pinkState[5] = {0.f, 0.f, 0.f, 0.f, 0.f};

  // Noise generation methods
  float generateNoise(float seed);
  float generateBrownNoise(float whiteNoise);
  float generatePinkNoise(float whiteNoise);
};

void KAOS::process(float color, float bkIn, float pkIn) {
  // Generate proper white, brown, and pink noise
  float wNoise = generateNoise(noise);
  float brownNoise = generateBrownNoise(wNoise);
  float pinkNoise = generatePinkNoise(wNoise);

  // Crossfade between noise types: brown (0.0) → pink (0.5) → white (1.0)
  // Mathematical guarantee: coefficients always sum to 1.0, no phase cancellation
  float brownLvl, pinkLvl, whiteLvl;

  if (color < 0.f) {
    // Brown to Pink crossfade
    brownLvl = std::abs(color); // 1.0 → 0.0
    pinkLvl = 1.0f + color;     // 0.0 → 1.0
    whiteLvl = 0.0f;
  } else {
    // Pink to White crossfade
    brownLvl = 0.0f;
    pinkLvl = 1.0f - color; // 1.0 → 0.0
    whiteLvl = color;       // 0.0 → 1.0
  }

  noise = brownLvl * brownNoise + pinkLvl * pinkNoise + whiteLvl * wNoise;

  if (pkIn != -99.f)
    if (pKaosTrigger.process(pkIn))
      pKaosOut = pinkNoise;

  if (bkIn != -99.f)
    if (bKaosTrigger.process(bkIn))
      bKaosOut = brownNoise;
};
// ============================================================================
// SAMPLE AND HOLD CLASS - NOISE GENERATORS
// ============================================================================

float KAOS::generateNoise(float seed) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::normal_distribution<float> dis(0.0f, 1.0f);

  return dis(gen) * 1.5f;
}

float KAOS::generateBrownNoise(float whiteNoise) {
  // Brown noise: integrate White noise with leaky integrator
  // This creates a -6dB/octave (1/f²) spectrum
  const float leakage = 0.99f; // Prevents DC buildup
  brownState = brownState * leakage + whiteNoise * 0.1f;

  // scale limits output to narrower pp range than Pink noise
  return brownState;
}

float KAOS::generatePinkNoise(float whiteNoise) {
  // Paul Kellet's Pink noise algorithm
  // Uses multiple first-order filters to approximate 1/f spectrum
  pinkState[0] = 0.99886f * pinkState[0] + whiteNoise * 0.0555179f;
  pinkState[1] = 0.99332f * pinkState[1] + whiteNoise * 0.0750759f;
  pinkState[2] = 0.96900f * pinkState[2] + whiteNoise * 0.1538520f;
  pinkState[3] = 0.86650f * pinkState[3] + whiteNoise * 0.3104856f;
  pinkState[4] = 0.55000f * pinkState[4] + whiteNoise * 0.5329522f;

  float pink = pinkState[0] + pinkState[1] + pinkState[2] + pinkState[3] + pinkState[4] +
               whiteNoise * 0.115926f;

  // Scale output to slightly narrower range than Brown noise
  return pink * 0.3f;
};

struct KI1H_KAOS : Module {
  enum ParamIds { NOISE_PARAM, NUM_PARAMS };

  enum InputIds { PKAOS_IN, BKAOS_IN, NUM_INPUTS };

  enum OutputIds { NOISE_OUT, PKAOS_OUT, BKAOS_OUT, NUM_OUTPUTS };

  KI1H_KAOS();
  void process(const ProcessArgs &args) override;

private:
  KAOS KAOS;
};

struct KI1H_KAOSWidget : ModuleWidget {
  KI1H_KAOSWidget(KI1H_KAOS *module);
};
KI1H_KAOS::KI1H_KAOS() {
  // ============================================================================
  // MODULE CONFIGURATION
  // ============================================================================
  config(KI1H_KAOS::NUM_PARAMS, KI1H_KAOS::NUM_INPUTS, KI1H_KAOS::NUM_OUTPUTS);
  configParam(NOISE_PARAM, -1.f, 1.f, 0.f, "Color");
  configOutput(NOISE_OUT, "NOISE OUT");
  configInput(PKAOS_IN, "Chaos 1 Trig");
  configInput(BKAOS_IN, "Chaos 2 Trig");
  configOutput(PKAOS_OUT, "Chaos 1 Out");
  configOutput(BKAOS_OUT, "Chaos 2 Out");
};

void KI1H_KAOS::process(const ProcessArgs &args) {
  float color = params[NOISE_PARAM].getValue();
  float bkIn = inputs[BKAOS_IN].isConnected() ? inputs[BKAOS_IN].getVoltage() : -99.f;
  float pkIn = inputs[PKAOS_IN].isConnected() ? inputs[PKAOS_IN].getVoltage() : -99.f;
  KAOS.process(color, bkIn, pkIn);
  outputs[NOISE_OUT].setVoltage(KAOS.getNoise());
  if (outputs[PKAOS_OUT].isConnected())
    outputs[PKAOS_OUT].setVoltage(KAOS.getpKaos());
  if (outputs[BKAOS_OUT].isConnected())
    outputs[BKAOS_OUT].setVoltage(KAOS.getbKaos());
};

KI1H_KAOSWidget::KI1H_KAOSWidget(KI1H_KAOS *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-KAOS.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_KAOS::NOISE_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                             KI1H_KAOS::NOISE_OUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[2])), module,
                                           KI1H_KAOS::PKAOS_IN));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[3])), module,
                                             KI1H_KAOS::PKAOS_OUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                           KI1H_KAOS::BKAOS_IN));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                             KI1H_KAOS::BKAOS_OUT));
};

Model *modelKI1H_KAOS = createModel<KI1H_KAOS, KI1H_KAOSWidget>("KI1H-KAOS");
