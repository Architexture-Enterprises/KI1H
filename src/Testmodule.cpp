#include "Testmodule.hpp"
#include "componentlibrary.hpp"
#include "window/Svg.hpp"

Testmodule::Testmodule() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
  configParam(PFINE_PARAM, 0.f, 1.f, 0.5f, "Detune", "Hz", std::pow(2, 1.f / 12.f),
              dsp::FREQ_C4, 0.f);
  configParam(PCOURSE_PARAM, 0.f, 1.f, 0.5f, "Frequency", " Hz", 7.f,
              dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);

  auto waveParam =
      configSwitch(WAVE_PARAM, 0.f, 3.f, 0.f, "Wave", {"Sin", "Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;

  configInput(PITCH_INPUT, "1V/oct pitch");
  configOutput(WAVE_OUT, "Waveform");
}

void Testmodule::process(const ProcessArgs &args) {
  // const Waves waveform = (Waves) params[WAVE_PARAM].getValue();

  // Compute the frequency from the pitch parameter and input
  float pitch = params[PFINE_PARAM].getValue() + params[PCOURSE_PARAM].getValue();
  pitch += inputs[PITCH_INPUT].getVoltage();
  // The default frequency is C4 = 261.6256f
  float freq = dsp::FREQ_C0* std::pow(2.f, pitch);

  // Accumulate the phase
  phase += freq * args.sampleTime;

  if (phase >= 1.f)
    phase -= 1.f;

  float wave = params[WAVE_PARAM].getValue();
  // Calculate Triangle output
  // Saw output
  // and pulse width variable out

  // float saw = phase - 0.5; 
  // although this works it halves the scale of the waveform
  // better approach
  float saw = phase * 2.f - 1.f;


  // Compute the sine output
  float sine = std::sin(2.f * M_PI * phase);


  float triangle;
  if (phase < 0.5f)
    // Phase is 0.25
    // 0.25 * 4 = 1
    // 1 - 1 = 0
    triangle = phase * 4.f - 1.f; // Rising: 0→0.5 becomes -1→+1
  else
    // Phase is 0.5 
    // 3 - 0.5 * 4 = 1
    triangle = 3.f - phase * 4.f; // Falling: 0.5→1 becomes +1→-1

  // Pulse width
  float pulseWidth = params[PULSEWIDTH_PARAM].getValue();

  float square;
  // since .5 is the middle of the phase
  // we divide to top/ bottom at this point
  if (phase > pulseWidth)
    square = -1.f;
  else
    square = 1.f;

  float waveOut;
  if (wave == 0)
    waveOut = sine;
  else if (wave == 1)
    waveOut = triangle;
  else if (wave == 2)
    waveOut = saw;
  else
    waveOut = square;

  // Audio signals are typically +/-5V
  // https://vcvrack.com/manual/VoltageStandards
  outputs[WAVE_OUT].setVoltage(5.f * waveOut);

  // Blink light at the same frequency as the oscillator
  blinkPhase += freq * args.sampleTime;
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
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(30.48, 46)), module, Testmodule::PCOURSE_PARAM));
  addParam(
      createParamCentered<RoundBlackKnob>(mm2px(Vec(45.72, 46)), module, Testmodule::PULSEWIDTH_PARAM));
  addParam(
      createParamCentered<BefacoSwitch>(mm2px(Vec(30.48, 66)), module, Testmodule::WAVE_PARAM));

  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(15.24, 66)), module, Testmodule::PITCH_INPUT));

  addOutput(
      createOutputCentered<PJ301MPort>(mm2px(Vec(45.72, 66)), module, Testmodule::WAVE_OUT));

  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(15.24, 25.81)), module,
                                                      Testmodule::BLINK_LIGHT));
}

Model *modelTestmodule = createModel<Testmodule, TestmoduleWidget>("testmodule");
