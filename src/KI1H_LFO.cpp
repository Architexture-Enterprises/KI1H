#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "plugin.hpp"

// ============================================================================
// LFO CLASS DEFINITION
// ============================================================================
class LFO {
public:
  void process(float pitch, int waveType, float sampletime);
  float getOutput() const {
    return output;
  };

private:
  float output = 0.f;
  float phase = 0.f;

  float generateTriangle(float phase);
  float generateSaw(float phase);
  float generateSquare(float phase);
};

// ============================================================================
// LFO MODULE DEFINITION
// ============================================================================
struct KI1H_LFO : Module {
  enum ParamIds { RATE1_PARAM, WAVE1_PARAM, RATE2_PARAM, WAVE2_PARAM, NUM_PARAMS };
  enum InputIds { CV1_INPUT, CV2_INPUT, NUM_INPUTS };
  enum OutputIds { WAVE1_OUT, WAVE2_OUT, NUM_OUTPUTS };
  enum Waves { WAVE_SQ, WAVE_SAW, WAVE_TRI };

  KI1H_LFO();
  void process(const ProcessArgs &args) override;

private:
  LFO lfo1, lfo2;
  float CV_SCALE = 5.f;
};

// ============================================================================
// LFO WIDGET DEFINITION
// ============================================================================
struct KI1H_LFOWidget : ModuleWidget {
  KI1H_LFOWidget(KI1H_LFO *module);
};
void LFO::process(float pitch, int waveType, float sampleTime) {

  float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);

  // ============================================================================
  // PHASE ACCUMULATION
  // ============================================================================
  // Normal phase accumulation
  phase += freq * sampleTime;
  if (phase >= 1.f)
    phase -= 1.f;

  // ============================================================================
  // WAVEFORM GENERATION
  // ============================================================================
  // Generate waveform based on type
  switch (waveType) {
  case 0:
    output = generateTriangle(phase);
    break;
  case 1:
    output = generateSaw(phase);
    break;
  case 2:
    output = generateSquare(phase);
    break;
  default:
    output = 0.f;
  }
};

// ============================================================================
// LFO CLASS - WAVEFORM GENERATORS
// ============================================================================
float LFO::generateTriangle(float ph) {
  if (ph < 0.5f)
    return ph * 4.f - 1.f; // Rising: 0→0.5 becomes -1→+1
  else
    return 3.f - ph * 4.f; // Falling: 0.5→1 becomes +1→-1
}

float LFO::generateSaw(float ph) {
  return ph * -2.f + 1.f; // Maps 0→1 phase to -1→+1
}

float LFO::generateSquare(float ph) {
  return (ph > 0.5f) ? -1.f : 1.f;
}

KI1H_LFO::KI1H_LFO() {
  // ============================================================================
  // LFO 1 - PARAMETER CONFIGURATION
  // ============================================================================

  config(KI1H_LFO::NUM_PARAMS, KI1H_LFO::NUM_INPUTS, KI1H_LFO::NUM_OUTPUTS);
  configParam(RATE1_PARAM, -6.f, -4.6f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configInput(CV1_INPUT, "Rate");
  auto waveParam =
      configSwitch(WAVE1_PARAM, 0.f, 2.f, 0.f, "Wave", {"Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;
  configOutput(WAVE1_OUT, "LFO1 Out");

  // ============================================================================
  // LFO 2 - PARAMETER CONFIGURATION
  // ============================================================================

  config(KI1H_LFO::NUM_PARAMS, KI1H_LFO::NUM_INPUTS, KI1H_LFO::NUM_OUTPUTS);
  configParam(RATE2_PARAM, -6.f, -4.6f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configInput(CV2_INPUT, "Rate");
  auto wave2Param =
      configSwitch(WAVE2_PARAM, 0.f, 2.f, 0.f, "Wave", {"Triangle", "Sawtooth", "Pulse"});
  wave2Param->snapEnabled = true;
  configOutput(WAVE2_OUT, "LFO2 Out");
};

void KI1H_LFO::process(const ProcessArgs &args) {
  // ============================================================================
  // LFO 1 - PITCH
  // ============================================================================
  float pitch1 = params[RATE1_PARAM].getValue();
  pitch1 += inputs[CV1_INPUT].getVoltage();
  int waveType1 = (int)params[WAVE1_PARAM].getValue();

  // ============================================================================
  // LFO 1 - PROCESS & OUTPUT
  // ============================================================================
  lfo1.process(pitch1, waveType1, args.sampleTime);
  outputs[WAVE1_OUT].setVoltage(CV_SCALE * lfo1.getOutput());

  // ============================================================================
  // LFO 2 - PITCH
  // ============================================================================
  float pitch2 = params[RATE2_PARAM].getValue();
  pitch2 += inputs[CV2_INPUT].getVoltage();
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  // ============================================================================
  // LFO 2 - PROCESS & OUTPUT
  // ============================================================================
  lfo2.process(pitch2, waveType2, args.sampleTime);
  outputs[WAVE2_OUT].setVoltage(CV_SCALE * lfo2.getOutput());
};

KI1H_LFOWidget::KI1H_LFOWidget(KI1H_LFO *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-VCO.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  // ============================================================================
  // LFO 1 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 30)), module, KI1H_LFO::RATE1_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 50)), module, KI1H_LFO::CV1_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(25, 50)), module, KI1H_LFO::WAVE1_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70, 50)), module, KI1H_LFO::WAVE1_OUT));

  // ============================================================================
  // LFO 2 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 30)), module, KI1H_LFO::RATE2_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 50)), module, KI1H_LFO::CV2_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(25, 50)), module, KI1H_LFO::WAVE2_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70, 50)), module, KI1H_LFO::WAVE2_OUT));
}

Model *modelKI1H_LFO = createModel<KI1H_LFO, KI1H_LFOWidget>("KI1H-LFO");
