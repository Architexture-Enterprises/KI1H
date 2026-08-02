#include "dsp.hpp"
#include "plugin.hpp"

/** One independent noise stream: a white generator plus the filter states that
colour it.

Each of the module's three outputs owns one. They were collapsed onto a single
generator, with the brown and pink shapers fed from the same white sample, so
noise, chaos 1 and chaos 2 all moved together — three views of one signal
rather than three chaos sources. Patching the noise jack into an S&H then gave
a voice that tracked the chaos outputs instead of wandering against them. */
struct NoiseSource {
  NoiseSource() {
    // Seeded from the global generator at construction time, which happens on
    // the UI thread — never from process(). Two separate draws per stream, so
    // no two instances share a starting state.
    rng.seed(rack::random::u64(), rack::random::u64());
  }

  // Per-instance noise stream. rack::random::local() backs the global
  // random::normal(), and the SDK documents it as no longer thread-local, so
  // sharing it would still race across engine worker threads.
  rack::random::Xoroshiro128Plus rng;

  // Brown noise state (integrator for 1/f² spectrum)
  float brownState = 0.f;

  // Pink noise state variables (Paul Kellet's algorithm)
  float pinkState[5] = {0.f, 0.f, 0.f, 0.f, 0.f};

  // Box-Muller produces two independent variates per pair of uniforms. Keeping
  // the second one halves the sqrt/log/sin work, which is what pays for
  // running three streams where there used to be one.
  float spare = 0.f;
  bool haveSpare = false;

  /** Uniform float in [0, 1) drawn from this instance's stream. */
  float uniform() {
    return (uint32_t)(rng() >> 32) * 2.32830629e-10f;
  }

  float white();
  float brown(float whiteNoise);
  float pink(float whiteNoise);
};

struct KAOS {
public:
  void process(float color, float bkIn, bool bkConn, float pkIn, bool pkConn);
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

  // One stream per output. Independent seeds are the whole point: the three
  // jacks are meant to be uncorrelated chaos sources.
  NoiseSource noiseSrc;
  NoiseSource chaos1Src;
  NoiseSource chaos2Src;
};

void KAOS::process(float color, float bkIn, bool bkConn, float pkIn, bool pkConn) {
  // Generate proper white, brown, and pink noise for the NOISE jack. These
  // three share a stream because the crossfade below blends between them: they
  // are one signal being recoloured, not three sources.
  float wNoise = noiseSrc.white();
  float brownNoise = noiseSrc.brown(wNoise);
  float pinkNoise = noiseSrc.pink(wNoise);

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

  // Chaos 1 (pink) and chaos 2 (brown) each run their own stream, so the value
  // held at one jack says nothing about the value held at the other or about
  // the noise jack. The shapers have to run every sample whether or not a
  // trigger fires — their filter states are what give pink and brown their
  // spectra, and sampling a filter that only advances on trigger edges would
  // just give a random walk of whatever the last white sample was.
  const float chaos1 = chaos1Src.pink(chaos1Src.white());
  const float chaos2 = chaos2Src.brown(chaos2Src.white());

  if (pkConn)
    if (pKaosTrigger.process(pkIn)) {
      pKaosOut = chaos1;
      // With no dedicated chaos-2 trigger patched, chaos 2 is held on chaos 1's
      // edges. It still holds its own stream's value, not chaos 1's.
      if (!bkConn)
        bKaosOut = chaos2;
    }

  if (bkConn)
    if (bKaosTrigger.process(bkIn))
      bKaosOut = chaos2;
}
// ============================================================================
// NOISE SOURCE - GENERATORS
// ============================================================================

float NoiseSource::white() {
  // Box-Muller, matching the distribution rack::random::normal() produces. The
  // sine and cosine legs are two independent variates, so returning one and
  // banking the other costs a cosine and saves a whole sqrt/log/sin on the
  // next call.
  if (haveSpare) {
    haveSpare = false;
    return spare;
  }

  const float radius = std::sqrt(-2.f * std::log(1.f - uniform()));
  const float theta = 2.f * ki1h::PI * uniform();
  spare = radius * std::cos(theta) * 1.5f;
  haveSpare = true;
  return radius * std::sin(theta) * 1.5f;
}

float NoiseSource::brown(float whiteNoise) {
  // Brown noise: integrate White noise with leaky integrator
  // This creates a -6dB/octave (1/f²) spectrum
  const float leakage = 0.99f; // Prevents DC buildup
  brownState = brownState * leakage + whiteNoise * 0.1f;

  // scale limits output to narrower pp range than Pink noise
  return brownState;
}

float NoiseSource::pink(float whiteNoise) {
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
}

struct KI1H_KAOS : Module {
  enum ParamIds { NOISE_PARAM, NUM_PARAMS };

  enum InputIds { PKAOS_INPUT, BKAOS_INPUT, NUM_INPUTS };

  enum OutputIds { NOISE_OUTPUT, PKAOS_OUTPUT, BKAOS_OUTPUT, NUM_OUTPUTS };

  KI1H_KAOS();
  void process(const ProcessArgs &args) override;

private:
  KAOS kaos;
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
  configOutput(NOISE_OUTPUT, "NOISE OUT");
  configInput(PKAOS_INPUT, "Chaos 1 Trig");
  configInput(BKAOS_INPUT, "Chaos 2 Trig");
  configOutput(PKAOS_OUTPUT, "Chaos 1 Out");
  configOutput(BKAOS_OUTPUT, "Chaos 2 Out");
}

void KI1H_KAOS::process(const ProcessArgs &args) {
  float color = params[NOISE_PARAM].getValue();
  const bool bkConn = inputs[BKAOS_INPUT].isConnected();
  const bool pkConn = inputs[PKAOS_INPUT].isConnected();
  kaos.process(color, inputs[BKAOS_INPUT].getVoltage(), bkConn, inputs[PKAOS_INPUT].getVoltage(),
               pkConn);
  outputs[NOISE_OUTPUT].setVoltage(kaos.getNoise());
  if (outputs[PKAOS_OUTPUT].isConnected())
    outputs[PKAOS_OUTPUT].setVoltage(kaos.getpKaos());
  if (outputs[BKAOS_OUTPUT].isConnected())
    outputs[BKAOS_OUTPUT].setVoltage(kaos.getbKaos());
}

KI1H_KAOSWidget::KI1H_KAOSWidget(KI1H_KAOS *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-KAOS.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_KAOS::NOISE_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                             KI1H_KAOS::NOISE_OUTPUT));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[2])), module,
                                              KI1H_KAOS::PKAOS_INPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[0], ROWS[3])), module,
                                              KI1H_KAOS::PKAOS_OUTPUT));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                              KI1H_KAOS::BKAOS_INPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                              KI1H_KAOS::BKAOS_OUTPUT));
}

Model *modelKI1H_KAOS = createModel<KI1H_KAOS, KI1H_KAOSWidget>("KI1H-KAOS");
