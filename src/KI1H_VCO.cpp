// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "plugin.hpp"

dsp::SchmittTrigger syncTrigger;

// ============================================================================
// OSCILLATOR BASE CLASS
// ============================================================================
class Oscillator {
public:
  float getOutput() const {
    return output;
  }
  float getBlink() const {
    return blinkPhase;
  }
  float getSin() const {
    return sin;
  }

protected:
  float phase = 0.f;
  float output = 0.f;
  float blinkPhase = 0.f;
  float sin = 0.f;

  void updatePhases(float freq, float sampleTime);
  float calculateFreq(float pitch);
  float generateSine(float ph);
  float generateSquare(float ph, float pw);
};

// ============================================================================
// RAW PURE WAVEFORM OSCILLATOR
// ============================================================================
class RawOscillator : public Oscillator {
public:
  void process(float pitch, float linFM, float pulseWidth, int waveType, float sampleTime);
  float getSub() const {
    return sub;
  }

private:
  float subPhase = 0.f;
  float sub = 0.f;

  float generateTriangle(float ph);
  float generateSaw(float ph);
  float generateSub(float ph);
};

// ============================================================================
// WAVESHAPING OSCILLATOR
// ============================================================================
class ShaperOscillator : public Oscillator {
public:
  void process(float pitch, float linFM, float softSync, float hardSync, float shape, int waveType,
               float sampleTime);

private:
  float generateShapedWave(float ph, float shape);
};

// ============================================================================
// VCO MODULE DEFINITION
// ============================================================================
struct KI1H_VCO : Module {
  enum ParamIds {
    PCOURSE_PARAM,
    PFINE_PARAM,
    PULSEWIDTH_PARAM,
    WAVE_PARAM,
    SYNC_PARAM,
    FM_PARAM,
    FM_SWITCH_PARAM,
    PCOURSE2_PARAM,
    PFINE2_PARAM,
    SHAPE_PARAM,
    WAVE2_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    PITCH_INPUT,
    PITCH2_INPUT,
    PW1_INPUT,
    SHAPE_INPUT,
    FM_INPUT,
    WEAK_SYNC,
    STRONG_SYNC,
    NUM_INPUTS
  };
  enum OutputIds { WAVE_OUT, WAVE2_OUT, SUB_OUT, NUM_OUTPUTS };
  enum LightIds { BLINK1_LIGHT, BLINK2_LIGHT, NUM_LIGHTS };
  enum Waves { WAVE_TRI, WAVE_SAW, WAVE_SQ, WAVE_PWM };

  KI1H_VCO();
  void process(const ProcessArgs &args) override;

private:
  RawOscillator osc1;
  ShaperOscillator osc2;
  float CV_SCALE = 5.f;
  float PWM_OFFSET = 5.5f;
};

// ============================================================================
// VCO WIDGET DEFINITION
// ============================================================================
struct KI1H_VCOWidget : ModuleWidget {
  KI1H_VCOWidget(KI1H_VCO *module);
};

// ============================================================================
// OSCILLATOR CLASS - SHARED FUNCTION
// ============================================================================
float Oscillator::calculateFreq(float pitch) {
  // Calculate frequency from pitch (1V/octave)
  return dsp::FREQ_C4 * std::pow(2.f, pitch);
}

void Oscillator::updatePhases(float freq, float sampleTime) {
  phase += freq * sampleTime;
  if (phase >= 1.f)
    phase -= 1.f;

  blinkPhase = phase;
}

float Oscillator::generateSine(float ph) {
  return std::sin(2.f * M_PI * ph);
}

float Oscillator::generateSquare(float ph, float pw) {
  // Clamp pulse width to prevent extreme values
  if (pw > 0.9f)
    pw = 0.9f;
  if (pw < 0.1f)
    pw = 0.1f;
  return (ph > pw) ? -1.f : 1.f;
}

// ============================================================================
// RAWOSCILLATOR CLASS
// ============================================================================
void RawOscillator::process(float pitch, float linFM, float pulseWidth, int waveType,
                            float sampleTime) {
  float freq = calculateFreq(pitch);
  float subFreq = freq / 2.f;
  updatePhases(freq, sampleTime);

  // Apply linear FM directly to frequency
  freq += freq * linFM * 0.1f;

  sin = generateSine(phase);

  subPhase += subFreq * sampleTime;
  if (subPhase >= 1.f)
    subPhase -= 1.f;

  sub = generateSub(subPhase);

  switch (waveType) {
  case 0:
    output = generateTriangle(phase);
    break;
  case 1:
    output = generateSaw(phase);
    break;
  case 2:
    output = generateSquare(phase, pulseWidth);
    break;
  default:
    output = 0.f;
  }
}

float RawOscillator::generateTriangle(float ph) {
  if (ph < 0.5f)
    return ph * 4.f - 1.f; // Rising: 0→0.5 becomes -1→+1
  else
    return 3.f - ph * 4.f; // Falling: 0.5→1 becomes +1→-1
}

float RawOscillator::generateSaw(float ph) {
  return ph * -2.f + 1.f; // Maps 0→1 phase to -1→+1
}

float RawOscillator::generateSub(float ph) {
  return (ph > 0.5f) ? -1.f : 1.f;
}

// ============================================================================
// SHAPEROSCILLATOR CLASS
// ============================================================================
void ShaperOscillator::process(float pitch, float linFM, float softSync, float hardSync,
                               float shape, int waveType, float sampleTime) {
  float freq = calculateFreq(pitch);
  updatePhases(freq, sampleTime);

  // Apply linear FM directly to frequency
  freq += freq * linFM * 0.1f;
  // ============================================================================
  // SYNC PROCESSING
  // ============================================================================
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

  sin = generateSine(phase);

  switch (waveType) {
  case 0:
    output = generateShapedWave(phase, shape);
    break;
  case 1:
    output = generateSquare(phase, shape);
    break;
  default:
    output = 0.f;
  }
}

float ShaperOscillator::generateShapedWave(float ph, float shape) {
  // Start with saw core
  float saw = ph * 2.f - 1.f; // -1 to +1 sawtooth
                              //
  float harmonicReduction = std::abs(1.f - shape);

  // Apply harmonic reduction using a simple low-pass-like formula
  // harmonicReduction: 0.0 = full saw, 1.0 = approaching sine

  if (harmonicReduction < 0.01f) {
    return saw; // Pure sawtooth
  }

  // Method 1: Fourier series reduction
  float result = 0.f;
  int maxHarmonics = (int)(8.f * (1.f - harmonicReduction)) + 1;

  for (int h = 1; h <= maxHarmonics; h++) {
    float amplitude = 1.f / h; // Sawtooth harmonic series
    float harmonicGain = std::pow(1.f - harmonicReduction, h - 1);
    result += amplitude * harmonicGain * std::sin(2.f * M_PI * ph * h);
  }

  return result;
}

// ============================================================================
// MODULE CONSTRUCTOR - PARAMETER & I/O CONFIGURATION
// ============================================================================
KI1H_VCO::KI1H_VCO() {
  config(KI1H_VCO::NUM_PARAMS, KI1H_VCO::NUM_INPUTS, KI1H_VCO::NUM_OUTPUTS, KI1H_VCO::NUM_LIGHTS);

  // ============================================================================
  // OSCILLATOR 1 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(PFINE_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(PULSEWIDTH_PARAM, 0.1f, 0.9f, 0.5f, "Pulse Width", " %", 0.f, 100.f, 0.f);
  auto waveParam =
      configSwitch(WAVE_PARAM, 0.f, 2.f, 0.f, "Wave", {"Triangle", "Sawtooth", "Pulse"});
  waveParam->snapEnabled = true;

  // ============================================================================
  // OSCILLATOR 1 - INPUT/OUTPUT CONFIGURATION
  // ============================================================================
  configInput(PITCH_INPUT, "1V/oct pitch");
  configInput(PW1_INPUT, "Pulsewidth");
  configOutput(WAVE_OUT, "Waveform");
  configOutput(SUB_OUT, "Sub");

  // ============================================================================
  // OSCILLATOR 2 - PARAMETER CONFIGURATION
  // ============================================================================
  configParam(PFINE2_PARAM, -0.5f, 0.5f, 0.f, "Detune", " cents", 0.f, 100.f, 0.f);
  configParam(PCOURSE2_PARAM, -4.6f, 5.2f, 0.f, "Frequency", " Hz", 2.f, dsp::FREQ_C4, 0.f);
  configParam(SHAPE_PARAM, 0.1f, 0.9f, 0.5f, "Shape", " %", 0.f, 100.f, 0.f);
  auto waveParam2 = configSwitch(WAVE2_PARAM, 0.f, 1.f, 0.f, "Wave", {"Sin-Saw", "Pulse"});
  waveParam2->snapEnabled = true;

  // ============================================================================
  // SYNC & FM PARAMETER CONFIGURATION
  // ============================================================================
  auto syncParam = configSwitch(SYNC_PARAM, 0.f, 2.f, 1.f, "Sync", {"Weak", "OFF", "Strong"});
  syncParam->snapEnabled = true;
  configParam(FM_PARAM, 0.f, 1.f, 0.f, "FM", " %", 0.f, 100.f, 0.f);
  auto fmSwitch = configSwitch(FM_SWITCH_PARAM, 0.f, 2.f, 0.f, "FM", {"OFF", "LIN", "LOG"});
  fmSwitch->snapEnabled = true;

  // ============================================================================
  // OSCILLATOR 2 - INPUT/OUTPUT CONFIGURATION
  // ============================================================================
  configInput(PITCH2_INPUT, "1V/oct pitch");
  configInput(SHAPE_INPUT, "Shape");
  configInput(WEAK_SYNC, "Soft sync");
  configInput(STRONG_SYNC, "Hard sync");
  configInput(FM_INPUT, "FM");
  configOutput(WAVE2_OUT, "Waveform");
}

void KI1H_VCO::process(const ProcessArgs &args) {
  // ============================================================================
  // OSCILLATOR 1 - PITCH & PWM PROCESSING
  // ============================================================================
  float pitch1 = params[PFINE_PARAM].getValue() + params[PCOURSE_PARAM].getValue();
  pitch1 += inputs[PITCH_INPUT].getVoltage();
  float pwm1 = 0;
  if (inputs[PW1_INPUT].isConnected())
    pwm1 = inputs[PW1_INPUT].getVoltage() / PWM_OFFSET;
  float pulseWidth1 = params[PULSEWIDTH_PARAM].getValue();
  int waveType1 = (int)params[WAVE_PARAM].getValue();

  // ============================================================================
  // OSCILLATOR 1 - PROCESS & OUTPUT
  // ============================================================================
  osc1.process(pitch1, 0.f, pulseWidth1 + pwm1, waveType1, args.sampleTime);
  outputs[WAVE_OUT].setVoltage(CV_SCALE * osc1.getOutput());
  outputs[SUB_OUT].setVoltage(CV_SCALE * osc1.getSub());

  // ============================================================================
  // OSCILLATOR 2 - PITCH & SYNC SETUP
  // ============================================================================
  int syncVal = (int)params[SYNC_PARAM].getValue();
  float pitch2 = params[PFINE2_PARAM].getValue() + params[PCOURSE2_PARAM].getValue();
  pitch2 += inputs[PITCH2_INPUT].getVoltage();

  // ============================================================================
  // OSCILLATOR 2 - FM PROCESSING
  // ============================================================================
  int fmSwitch = (int)params[FM_SWITCH_PARAM].getValue();
  // FM source selection: external input overrides internal hardwire from Osc1
  float fmVal = 1.f;
  if (inputs[FM_INPUT].isConnected())
    fmVal = inputs[FM_INPUT].getVoltage();
  else
    fmVal = osc1.getSin() * CV_SCALE;

  // FM mode switching: 0=off, 1=linear, 2=exponential
  float linFM = 0.f;
  if (fmSwitch == 1)
    linFM = fmVal * params[FM_PARAM].getValue();
  if (fmSwitch == 2)
    pitch2 += fmVal * params[FM_PARAM].getValue() * 0.2f;

  // ============================================================================
  // OSCILLATOR 2 - PWM PROCESSING
  // ============================================================================
  float shapeIn = 0;
  if (inputs[SHAPE_INPUT].isConnected())
    shapeIn = inputs[SHAPE_INPUT].getVoltage() / PWM_OFFSET;

  // ============================================================================
  // OSCILLATOR 2 - SYNC PROCESSING
  // ============================================================================
  float softSync = 0.f;
  if (inputs[WEAK_SYNC].isConnected())
    softSync = inputs[WEAK_SYNC].getVoltage();
  float hardSync = 0.f;
  if (inputs[STRONG_SYNC].isConnected())
    hardSync = inputs[STRONG_SYNC].getVoltage();

  // Internal sync routing from Osc1
  if (syncVal == 0)
    softSync += (CV_SCALE * osc1.getOutput());
  else if (syncVal == 2)
    hardSync += (CV_SCALE * osc1.getOutput());

  // ============================================================================
  // OSCILLATOR 2 - PROCESS & OUTPUT
  // ============================================================================
  float shape = params[SHAPE_PARAM].getValue();
  int waveType2 = (int)params[WAVE2_PARAM].getValue();

  osc2.process(pitch2, linFM, softSync, hardSync, shape + shapeIn, waveType2, args.sampleTime);
  outputs[WAVE2_OUT].setVoltage(CV_SCALE * osc2.getOutput());

  // ============================================================================
  // STATUS LIGHT PROCESSING
  // ============================================================================
  lights[BLINK1_LIGHT].setBrightness(osc1.getBlink() < 0.5f ? 1.f : 0.f);
  lights[BLINK2_LIGHT].setBrightness(osc2.getBlink() < 0.5f ? 1.f : 0.f);
}

KI1H_VCOWidget::KI1H_VCOWidget(KI1H_VCO *module) {
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
  // OSCILLATOR 1 - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                               KI1H_VCO::PFINE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                               KI1H_VCO::PCOURSE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[0])), module,
                                               KI1H_VCO::PULSEWIDTH_PARAM));

  // ============================================================================
  // OSCILLATORS - STATUS LIGHT
  // ============================================================================
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[1] - HALF_R)), module, KI1H_VCO::BLINK1_LIGHT));
  addChild(createLightCentered<MediumLight<RedLight>>(
      mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[5] - HALF_R)), module, KI1H_VCO::BLINK2_LIGHT));

  // ============================================================================
  // OSCILLATOR 1 - INPUTS, CONTROLS & OUTPUT
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                           KI1H_VCO::PITCH_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                             KI1H_VCO::WAVE_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                           KI1H_VCO::PW1_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[0])), module,
                                             KI1H_VCO::WAVE_OUT));
  addOutput(
      createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[1])), module, KI1H_VCO::SUB_OUT));

  // ============================================================================
  // OSCILLATOR 2 - SYNC & FM CONTROLS
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[3])), module,
                                           KI1H_VCO::WEAK_SYNC));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[3])), module,
                                             KI1H_VCO::SYNC_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[3])), module,
                                           KI1H_VCO::STRONG_SYNC));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[3], ROWS[3])), module,
                                             KI1H_VCO::FM_SWITCH_PARAM));

  // ============================================================================
  // OSCILLATOR 2 - CONTROL KNOBS & OUTPUT
  // ============================================================================
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[0], ROWS[4])), module,
                                               KI1H_VCO::PFINE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[1], ROWS[4])), module,
                                               KI1H_VCO::PCOURSE2_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[2], ROWS[4])), module,
                                               KI1H_VCO::SHAPE_PARAM));
  addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[3], ROWS[4])), module,
                                               KI1H_VCO::FM_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[4])), module,
                                             KI1H_VCO::WAVE2_OUT));

  // ============================================================================
  // OSCILLATOR 2 - INPUTS & WAVE CONTROL
  // ============================================================================
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                           KI1H_VCO::PITCH2_INPUT));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                             KI1H_VCO::WAVE2_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                           KI1H_VCO::SHAPE_INPUT));
  addInput(
      createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3], ROWS[5])), module, KI1H_VCO::FM_INPUT));
}

Model *modelKI1H_VCO = createModel<KI1H_VCO, KI1H_VCOWidget>("KI1H-VCO");
