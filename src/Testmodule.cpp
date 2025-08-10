#include "Testmodule.hpp"
#include "componentlibrary.hpp"
#include "dsp/common.hpp"
#include "dsp/digital.hpp"
#include "window/Svg.hpp"

// Oscillator class implementation
void Oscillator::process(float pitch, float softSync, float hardSync, float pulseWidth,
                         int waveType, float sampleTime) {
  // Calculate frequency from pitch (1V/octave)
  float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);

  float syncAmount = 0.2f;
  // Sync signal modulates the frequency directly
  float baseFreq = dsp::FREQ_C4 * std::pow(2.f, pitch);
  float syncModulation = softSync * syncAmount * 0.1f; // Scale appropriately
  float modulatedFreq = baseFreq * (1.f + syncModulation);

  // Use modulated frequency for phase accumulation
  phase += modulatedFreq * sampleTime;

  dsp::SchmittTrigger syncTrigger;
  if (syncTrigger.process(hardSync)) {
    phase = 0.f;
  }

  // Accumulate phase
  phase += freq * sampleTime;
  if (phase >= 1.f)
    phase -= 1.f;

  // Generate waveform based on type
  switch (waveType) {
  case 0:
    output = generateSine(phase);
    break;
  case 1:
    output = generateTriangle(phase);
    break;
  case 2:
    output = generateSaw(phase);
    break;
  case 3:
    output = generateSquare(phase, pulseWidth);
    break;
  default:
    output = 0.f;
  }
}

float Oscillator::generateSine(float ph) {
  return std::sin(2.f * M_PI * ph);
}

float Oscillator::generateTriangle(float ph) {
  if (ph < 0.5f)
    return ph * 4.f - 1.f; // Rising: 0→0.5 becomes -1→+1
  else
    return 3.f - ph * 4.f; // Falling: 0.5→1 becomes +1→-1
}

float Oscillator::generateSaw(float ph) {
  return ph * -2.f + 1.f; // Maps 0→1 phase to -1→+1
}

float Oscillator::generateSquare(float ph, float pw) {
  return (ph > pw) ? -1.f : 1.f;
}

Testmodule::Testmodule() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
  configParam(PFINE_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);

  auto waveParam =
      configSwitch(WAVE_PARAM, 0.f, 3.f, 0.f, "Wave", {"Sin", "Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;

  configInput(PITCH_INPUT, "1V/oct pitch");
  configOutput(WAVE_OUT, "Waveform");
  configParam(PFINE2_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE2_PARAM, -4.f, 3.f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH2_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);

  auto waveParam2 =
      configSwitch(WAVE2_PARAM, 0.f, 3.f, 0.f, "Wave", {"Sin", "Triangle", "Sawtooth", "Pulse"});
  waveParam2->snapEnabled = true;

  configInput(PITCH2_INPUT, "1V/oct pitch");
  configInput(WEAK_SYNC, "Soft sync");
  configInput(STRONG_SYNC, "Hard sync");
  configOutput(WAVE2_OUT, "Waveform");
}

void Testmodule::process(const ProcessArgs &args) {
  // Process Oscillator 1
  float pitch1 = params[PFINE_PARAM].getValue() + params[PCOURSE_PARAM].getValue();
  pitch1 += inputs[PITCH_INPUT].getVoltage();
  float pulseWidth1 = params[PULSEWIDTH_PARAM].getValue();
  int waveType1 = (int)params[WAVE_PARAM].getValue();

  osc1.process(pitch1, 0.f, 0.f, pulseWidth1, waveType1, args.sampleTime);
  outputs[WAVE_OUT].setVoltage(5.f * osc1.getOutput());

  // Process Oscillator 2
  float pitch2 = params[PFINE2_PARAM].getValue() + params[PCOURSE2_PARAM].getValue();
  pitch2 += inputs[PITCH2_INPUT].getVoltage();
  float softSync = -99.f;
  if (inputs[WEAK_SYNC].isConnected())
    softSync = inputs[WEAK_SYNC].getVoltage();
  float hardSync = -99.f;
  if (inputs[STRONG_SYNC].isConnected())
    hardSync = inputs[STRONG_SYNC].getVoltage();
  float pulseWidth2 = params[PULSEWIDTH2_PARAM].getValue();
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  osc2.process(pitch2, softSync, hardSync, pulseWidth2, waveType2, args.sampleTime);
  outputs[WAVE2_OUT].setVoltage(5.f * osc2.getOutput());

  // Blink light at the same frequency as oscillator 1
  float freq1 = dsp::FREQ_C4 * std::pow(2.f, pitch1);
  blinkPhase += freq1 * args.sampleTime;
  if (blinkPhase >= 1.f)
    blinkPhase -= 1.f;
  lights[BLINK_LIGHT].setBrightness(blinkPhase < 0.5f ? 1.f : 0.f);
}

TestmoduleWidget::TestmoduleWidget(Testmodule *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/MyModule.svg")));

  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 46)), module, Testmodule::PFINE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 46)), module,
                                               Testmodule::PCOURSE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(45.72, 46)), module,
                                               Testmodule::PULSEWIDTH_PARAM));
  addParam(
      createParamCentered<BefacoSwitch>(mm2px(Vec(30.48, 66)), module, Testmodule::WAVE_PARAM));

  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 66)), module, Testmodule::PITCH_INPUT));

  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(45.72, 66)), module, Testmodule::WAVE_OUT));

  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(15.24, 25.81)), module,
                                                      Testmodule::BLINK_LIGHT));

  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(15.24, 86)), module, Testmodule::PFINE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 86)), module,
                                               Testmodule::PCOURSE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(45.72, 86)), module,
                                               Testmodule::PULSEWIDTH2_PARAM));
  addParam(
      createParamCentered<BefacoSwitch>(mm2px(Vec(30.48, 106)), module, Testmodule::WAVE2_PARAM));

  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 106)), module, Testmodule::PITCH2_INPUT));

  addOutput(
      createOutputCentered<PJ301MPort>(mm2px(Vec(45.72, 106)), module, Testmodule::WAVE2_OUT));
}

Model *modelTestmodule = createModel<Testmodule, TestmoduleWidget>("testmodule");
