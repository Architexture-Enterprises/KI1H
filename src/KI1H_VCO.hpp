#pragma once
#include "plugin.hpp"

// Reusable oscillator class for DRY code
class Oscillator {
public:
  void process(float pitch, float softSync, float hardSync, float pulseWidth, int waveType,
               float sampleTime);
  float getOutput() const {
    return output;
  }

private:
  float phase = 0.f;
  float output = 0.f;

  float generateSine(float ph);
  float generateTriangle(float ph);
  float generateSaw(float ph);
  float generateSquare(float ph, float pw);
};

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
    PULSEWIDTH2_PARAM,
    WAVE2_PARAM,
    NUM_PARAMS
  };
  enum InputIds {
    PITCH_INPUT,
    PITCH2_INPUT,
    PW1_INPUT,
    PW2_INPUT,
    FM_INPUT,
    WEAK_SYNC,
    STRONG_SYNC,
    NUM_INPUTS
  };
  enum OutputIds { WAVE_OUT, WAVE2_OUT, NUM_OUTPUTS };
  enum LightIds { BLINK1_LIGHT, BLINK2_LIGHT, NUM_LIGHTS };
  enum Waves { WAVE_TRI, WAVE_SAW, WAVE_SQ, WAVE_PWM };

  KI1H_VCO();
  void process(const ProcessArgs &args) override;

private:
  Oscillator osc1, osc2;
  float blinkPhase = 0.f;
};

struct KI1H_VCOWidget : ModuleWidget {
  KI1H_VCOWidget(KI1H_VCO *module);
};
