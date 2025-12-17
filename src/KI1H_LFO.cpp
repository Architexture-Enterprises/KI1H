#include "plugin.hpp"
#include "random"

// ============================================================================
// LFO CLASS DEFINITION
// ============================================================================
struct LFO {
  void process(float pitch, int waveType, float sampletime);
  virtual float getOutput() const {
    return output;
  };
  float getBlink() const {
    return phase;
  }

  float output = 0.f;
  float phase = 0.f;

  float generateSine(float phase);
  float generateTriangle(float phase);
  float generateSaw(float phase);
  float generateSquare(float phase);
  float generateRamp(float phase);
};

// ============================================================================
// SAMPLE AND HOLD CLASS DEFINITION (Inherits from LFO)
// ============================================================================
struct SampleAndHold : LFO {
public:
  void process(float pitch, float clockIn, float sampleRate, float sampleIn, bool sampInConn,
               int waveType, float lagTime, float sampleTime);
  float getOutput() const override {
    return laggedOutput;
  };
  float getClock() {
    return clockOutput;
  };
  float getBlink() const {
    return clockPhase;
  };

  float clockPhase = 0.f;
  float sampledValue = 0.f;
  float laggedOutput = 0.f;
  float clockOutput = 0.f;
  dsp::SchmittTrigger sampleTrigger;
};

// ============================================================================
// LFO MODULE DEFINITION
// ============================================================================
struct KI1H_LFO : Module {
  enum ParamIds {
    RATE1_PARAM,
    WAVE1_PARAM,
    RATE2_PARAM,
    WAVE2_PARAM,
    SRATE_PARAM,
    SWAVE_PARAM,
    SLAG_PARAM,
    NUM_PARAMS
  };
  enum InputIds { CV1_INPUT, CV2_INPUT, SAMP_IN, CLOCK_IN, NUM_INPUTS };
  enum OutputIds { WAVE1_OUT, WAVE2_OUT, CLOCK_OUT, SWAVE_OUT, NUM_OUTPUTS };
  enum LightIds { BLINK1_LIGHT, BLINK2_LIGHT, CLOCK_LIGHT, NUM_LIGHTS };

  KI1H_LFO();
  void process(const ProcessArgs &args) override;

private:
  LFO lfo1, lfo2;
  SampleAndHold SNH;
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
    output = generateSine(phase);
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
// SAMPLE AND HOLD PROCESS METHOD
// ============================================================================
void SampleAndHold::process(float pitch, float clockIn, float sampleRate, float sampleIn,
                            bool sampInConn, int sWaveType, float lagTime, float sampleTime) {

  float freq = dsp::FREQ_C4 * std::pow(2.f, pitch);

  float clockFreq = dsp::FREQ_C4 * std::pow(2.f, sampleRate);
  // ============================================================================
  // PHASE ACCUMULATION
  // ============================================================================
  phase += freq * sampleTime;
  if (phase >= 1.f)
    phase -= 1.f;

  clockPhase += clockFreq * sampleTime;
  if (clockPhase >= 1.f)
    clockPhase -= 1.f;

  // ============================================================================
  // S&H SPECIFIC WAVEFORM GENERATION
  // ============================================================================
  // Generate S&H waveforms (different from regular LFO waveforms)
  switch (sWaveType) {
  case 0:
    output = generateSaw(phase);
    break;
  case 1:
    output = generateRamp(phase);
    break;
  case 2:
    output = generateTriangle(phase);
    break;
  default:
    output = 0.f;
  }

  if (sampleRate == -1)
    clockOutput = clockIn;
  else
    clockOutput = generateSquare(clockPhase);
  // ============================================================================
  // SAMPLE ON TRIGGER RISING EDGE
  // ============================================================================
  // Use Schmitt trigger for robust edge detection with hysteresis
  if (sampleTrigger.process(clockOutput)) {
    // Sample the current oscillator output value on rising edge
    sampledValue = sampInConn ? sampleIn : output;
  }

  // ============================================================================
  // APPLY EXPONENTIAL LAG TO SAMPLED VALUE
  // ============================================================================
  // Calculate time constant for 99% settling in lagTime
  float timeConstant = lagTime / 4.605f;
  float alpha = 1.0f - exp(-sampleTime / timeConstant);

  // Apply lag filtering to the sampled value
  laggedOutput = alpha * sampledValue + (1.0f - alpha) * laggedOutput;
};

// ============================================================================
// LFO CLASS - WAVEFORM GENERATORS
// ============================================================================
float LFO::generateSine(float ph) {
  return std::sin(2.f * M_PI * ph);
}

float LFO::generateTriangle(float ph) {
  if (ph < 0.5f)
    return ph * 4.f - 1.f; // Rising: 0→0.5 becomes -1→+1
  else
    return 3.f - ph * 4.f; // Falling: 0.5→1 becomes +1→-1
}

float LFO::generateSaw(float ph) {
  return ph * -2.f + 1.f; // Maps 0→1 phase to -1→+1
}

float LFO::generateRamp(float ph) {
  return ph * 2.f - 1.f;
}

float LFO::generateSquare(float ph) {
  return (ph > 0.5f) ? -1.f : 1.f;
}

KI1H_LFO::KI1H_LFO() {
  // ============================================================================
  // MODULE CONFIGURATION
  // ============================================================================
  config(KI1H_LFO::NUM_PARAMS, KI1H_LFO::NUM_INPUTS, KI1H_LFO::NUM_OUTPUTS, KI1H_LFO::NUM_LIGHTS);

  // ============================================================================
  // LFO 1 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(RATE1_PARAM, -10.f, -3.4f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configInput(CV1_INPUT, "Rate");
  auto waveParam = configSwitch(WAVE1_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sine", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;
  configOutput(WAVE1_OUT, "LFO1 Out");

  // ============================================================================
  // LFO 2 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(RATE2_PARAM, -10.f, -3.4f, -5.3f, "Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  configInput(CV2_INPUT, "Rate");
  auto wave2Param = configSwitch(WAVE2_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sine", "Sawtooth", "Pulse"});
  wave2Param->snapEnabled = true;
  configOutput(WAVE2_OUT, "LFO2 Out");

  // ============================================================================
  // S&H - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(SRATE_PARAM, -10.f, -3.4f, -5.3f, "Sample Rate", "Hz", 2.f, dsp::FREQ_C4, 0.f);
  auto sWaveParam =
      configSwitch(SWAVE_PARAM, 0.f, 2.f, 0.f, "Wave", {"Sawtooth", "Ramp", "Triangle"});
  sWaveParam->snapEnabled = true;
  configParam(SLAG_PARAM, 0.0f, 0.2f, 0.f, "Lag", "ms", 0.f, 1000.f, 0.f);
  configInput(SAMP_IN, "Ext. In");
  configInput(CLOCK_IN, "Clock in");
  configOutput(SWAVE_OUT, "S&H Out");
  configOutput(CLOCK_OUT, "Clock Out");
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

  // ============================================================================
  // S&H - PARAMETERS & PROCESSING
  // ============================================================================
  float sRate = params[SRATE_PARAM].getValue();
  int sWaveType = (int)params[SWAVE_PARAM].getValue();
  float lagTime = params[SLAG_PARAM].getValue();

  // Ensure minimum lag time to prevent division by zero
  lagTime = std::max(lagTime, 0.001f);

  // ============================================================================
  // SNH - PROCESS & OUTPUT
  // ============================================================================
  // Sample and Hold with Lag - Implementation details:
  // 1. Uses LFO2 oscillator as trigger source
  // 2. Samples on rising edge crossings (negative to positive)
  // 3. Applies exponential lag with tau = lagTime / 4.605 for 99% settling
  // 4. Models analog RC circuit with JFET buffer behavior
  float sampleIn = 0.f;
  bool ext = inputs[SAMP_IN].isConnected();
  if (ext)
    sampleIn = inputs[SAMP_IN].getVoltage() * 0.2f;
  float clockIn = inputs[CLOCK_IN].getVoltage();
  if (inputs[CLOCK_IN].isConnected())
    sRate = -1.f;

  SNH.process(pitch2, clockIn, sRate, sampleIn, ext, sWaveType, lagTime, args.sampleTime);
  outputs[SWAVE_OUT].setVoltage(CV_SCALE * SNH.getOutput());
  outputs[CLOCK_OUT].setVoltage(CV_SCALE * SNH.getClock());

  lights[BLINK1_LIGHT].setBrightness(lfo1.getBlink() < 0.5f ? 1.f : 0.f);
  lights[BLINK2_LIGHT].setBrightness(lfo2.getBlink() < 0.5f ? 1.f : 0.f);
  lights[CLOCK_LIGHT].setBrightness(SNH.getBlink() < 0.5f ? 1.f : 0.f);
};

KI1H_LFOWidget::KI1H_LFOWidget(KI1H_LFO *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-LFO.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  // ============================================================================
  // BLINKEN LIGHTS
  // ============================================================================
  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(COLUMNS[2], ROWS[3] - HALF_R)),
                                                      module, KI1H_LFO::BLINK1_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(mm2px(Vec(COLUMNS[2], ROWS[5] - HALF_R)),
                                                      module, KI1H_LFO::BLINK2_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[1] - HALF_R)), module, KI1H_LFO::CLOCK_LIGHT));

  // ============================================================================
  // LFO 1 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[3] - HALF_R)), module,
                                                  KI1H_LFO::RATE1_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[3])), module,
                                           KI1H_LFO::CV1_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[2], ROWS[2])), module,
                                             KI1H_LFO::WAVE1_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[3])), module,
                                             KI1H_LFO::WAVE1_OUT));

  // ============================================================================
  // LFO 2 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[5] - HALF_R)), module,
                                                  KI1H_LFO::RATE2_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                           KI1H_LFO::CV2_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                             KI1H_LFO::WAVE2_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                             KI1H_LFO::WAVE2_OUT));

  // ============================================================================
  // S&H - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                               KI1H_LFO::SRATE_PARAM));
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[1])), module, KI1H_LFO::CLOCK_IN));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_LFO::SLAG_PARAM));
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[1])), module, KI1H_LFO::SAMP_IN));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[1] - HALF_R)),
                                             module, KI1H_LFO::SWAVE_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                             KI1H_LFO::SWAVE_OUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                             KI1H_LFO::CLOCK_OUT));
}

Model *modelKI1H_LFO = createModel<KI1H_LFO, KI1H_LFOWidget>("KI1H-LFO");
