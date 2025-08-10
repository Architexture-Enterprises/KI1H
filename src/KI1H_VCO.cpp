#include "KI1H_VCO.hpp"
#include "componentlibrary.hpp"
#include "dsp/common.hpp"
#include "dsp/digital.hpp"
#include "helpers.hpp"
#include "window/Svg.hpp"
#include <cstddef>

dsp::SchmittTrigger syncTrigger;

// Oscillator class implementation
void Oscillator::process(float pitch, float softSync, float hardSync, float pulseWidth,
                         int waveType, float sampleTime) {
  // Calculate frequency from pitch (1V/octave)
  float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);

  // Hard sync - digital reset when sync signal crosses threshold
  if (syncTrigger.process(hardSync)) {
    phase = 0.f;
  }

  // Soft sync - analog-modeled continuous phase pulling
  // The sync signal creates a "force" that pulls the phase toward reset
  float syncPull = 0.f;
  if (softSync > 0.1f) { // Only pull when sync signal is above noise floor
    // Create exponential pull force - stronger as phase increases
    float pullStrength = softSync * 0.2f;    // Scale sync signal
    syncPull = pullStrength * phase * phase; // Quadratic pull (gets stronger near end of cycle)

    // Pull phase backward toward 0, creating the chaotic analog behavior
    phase -= syncPull * sampleTime * freq;

    // Prevent phase from going negative
    if (phase < 0.f)
      phase = 0.f;
  }

  // Normal phase accumulation
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
  if (pw > 0.9f)
    pw = 0.9f;
  if (pw < 0.1f)
    pw = 0.1f;
  return (ph > pw) ? -1.f : 1.f;
}

KI1H_VCO::KI1H_VCO() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
  configParam(PFINE_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);
  auto syncParam = configSwitch(SYNC_PARAM, 0.f, 2.f, 1.f, "Sync", {"Weak", "OFF", "Strong"});
  syncParam->snapEnabled = true;
  configParam(FM_PARAM, 0.f, 1.f, 0.5f, "FM", " %", 0.f, 100.f, 0.f);
  auto fmSwitch = configSwitch(FM_SWITCH_PARAM, 0.f, 2.f, 0.f, "FM", {"OFF", "LIN", "LOG"});
  fmSwitch->snapEnabled = true;
  auto waveParam =
      configSwitch(WAVE_PARAM, 0.f, 3.f, 0.f, "Wave", {"Sin", "Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;

  configInput(PITCH_INPUT, "1V/oct pitch");
  configInput(PW1_INPUT, "Pulsewidth");
  configOutput(WAVE_OUT, "Waveform");
  configParam(PFINE2_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE2_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH2_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);

  auto waveParam2 =
      configSwitch(WAVE2_PARAM, 0.f, 3.f, 0.f, "Wave", {"Sin", "Triangle", "Sawtooth", "Pulse"});
  waveParam2->snapEnabled = true;

  configInput(PITCH2_INPUT, "1V/oct pitch");
  configInput(PW2_INPUT, "Pulsewidth");
  configInput(WEAK_SYNC, "Soft sync");
  configInput(STRONG_SYNC, "Hard sync");
  configInput(FM_INPUT, "FM");
  configOutput(WAVE2_OUT, "Waveform");
}

void KI1H_VCO::process(const ProcessArgs &args) {
  // Process Oscillator 1
  float pitch1 = params[PFINE_PARAM].getValue() + params[PCOURSE_PARAM].getValue();
  pitch1 += inputs[PITCH_INPUT].getVoltage();
  float pwm1 = 0;
  if (inputs[PW1_INPUT].isConnected())
    pwm1 = inputs[PW1_INPUT].getVoltage() / 5.5f;
  float pulseWidth1 = params[PULSEWIDTH_PARAM].getValue();
  int waveType1 = (int)params[WAVE_PARAM].getValue();

  osc1.process(pitch1, 0.f, 0.f, pulseWidth1 + pwm1, waveType1, args.sampleTime);
  outputs[WAVE_OUT].setVoltage(5.f * osc1.getOutput());

  // Process Oscillator 2
  int syncVal = (int)params[SYNC_PARAM].getValue();
  float pitch2 = params[PFINE2_PARAM].getValue() + params[PCOURSE2_PARAM].getValue();
  pitch2 += inputs[PITCH2_INPUT].getVoltage();
  int fmSwitch = (int)params[FM_SWITCH_PARAM].getValue();
  float fmVal = 1.f;
  if (inputs[FM_INPUT].isConnected())
    fmVal = inputs[FM_INPUT].getVoltage();
  else
    fmVal = osc1.getOutput() * 5.f;
  if (fmSwitch > 0)
    pitch2 += fmVal * params[FM_PARAM].getValue();
  float pwm2 = 0;
  if (inputs[PW2_INPUT].isConnected())
    pwm2 = inputs[PW2_INPUT].getVoltage() / 5.5f;
  float softSync = 0.f;
  if (inputs[WEAK_SYNC].isConnected())
    softSync = inputs[WEAK_SYNC].getVoltage();
  float hardSync = 0.f;
  if (inputs[STRONG_SYNC].isConnected())
    hardSync = inputs[STRONG_SYNC].getVoltage();
  if (syncVal == 0)
    softSync += (5.f * osc1.getOutput());
  else if (syncVal == 2)
    hardSync += (5.f * osc1.getOutput());

  float pulseWidth2 = params[PULSEWIDTH2_PARAM].getValue();
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  osc2.process(pitch2, softSync, hardSync, pulseWidth2 + pwm2, waveType2, args.sampleTime);
  outputs[WAVE2_OUT].setVoltage(5.f * osc2.getOutput());

  // Blink light at the same frequency as oscillator 1
  float freq1 = dsp::FREQ_C4 * std::pow(2.f, pitch1);
  blinkPhase += freq1 * args.sampleTime;
  if (blinkPhase >= 1.f)
    blinkPhase -= 1.f;
  lights[BLINK1_LIGHT].setBrightness(blinkPhase < 0.5f ? 1.f : 0.f);
}

KI1H_VCOWidget::KI1H_VCOWidget(KI1H_VCO *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-VCO.svg")));

  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 25)), module, KI1H_VCO::PFINE_PARAM));
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(25, 25)), module, KI1H_VCO::PCOURSE_PARAM));
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(40, 25)), module, KI1H_VCO::PULSEWIDTH_PARAM));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(25, 45)), module, KI1H_VCO::WAVE_PARAM));

  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 45)), module, KI1H_VCO::PITCH_INPUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40, 45)), module, KI1H_VCO::PW1_INPUT));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(55, 45)), module, KI1H_VCO::FM_PARAM));

  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70, 45)), module, KI1H_VCO::WAVE_OUT));

  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(17.5, 35)), module,
                                                      KI1H_VCO::BLINK1_LIGHT));

  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10, 86)), module, KI1H_VCO::PFINE2_PARAM));
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(25, 86)), module, KI1H_VCO::PCOURSE2_PARAM));
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(40, 86)), module, KI1H_VCO::PULSEWIDTH2_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 65)), module, KI1H_VCO::WEAK_SYNC));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(25, 65)), module, KI1H_VCO::SYNC_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40, 65)), module, KI1H_VCO::STRONG_SYNC));
  addParam(
      createParamCentered<BefacoSwitch>(mm2px(Vec(55, 65)), module, KI1H_VCO::FM_SWITCH_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(55, 86)), module, KI1H_VCO::FM_INPUT));

  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10, 106)), module, KI1H_VCO::PITCH2_INPUT));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(40, 106)), module, KI1H_VCO::PW2_INPUT));

  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(25, 106)), module, KI1H_VCO::WAVE2_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(70, 106)), module, KI1H_VCO::WAVE2_OUT));
}

Model *modelKI1H_VCO = createModel<KI1H_VCO, KI1H_VCOWidget>("KI1H-VCO");
