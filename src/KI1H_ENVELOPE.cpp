
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "plugin.hpp"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// We want to make two AD and two ASR envelopes. When the AD out is not connected,
// The envelope section should behave as an AHDSR env, otherwise it should act as
// an AD env and an AR/ASR env with swichable behaviour

// ============================================================================
// CLASS DEFINITION
// ============================================================================
struct Envelope {

  enum Stage { STAGE_OFF, STAGE_ATTACK, STAGE_SUSTAIN, STAGE_RELEASE };
  float env = 0.f;
  float eoa = 0.f;
  float eor = 1.f;
};

struct ADEnvelope : Envelope {

  Stage stage = STAGE_OFF;
  float envState = 0.f;
  float attackTime = 0.1f, releaseTime = 0.1f;

  ADEnvelope() {};

  void retrigger() {
    stage = STAGE_ATTACK;

    env = envState = 0.f;
  }

  void processTransition(const bool held) {
    if (stage == STAGE_ATTACK) {
      if (envState >= 1.0f) {
        eoa = 1.f;
        eor = 0.f;
        env = envState = 1.0f;
        stage = STAGE_RELEASE;
      }
    } else if (stage == STAGE_RELEASE) {
      if (held) {
        eoa = 1.f;
      }
      if (envState <= 0.f) {
        eoa = 0.f;
        eor = 1.f;
        stage = STAGE_OFF;
        env = envState = 0.f;
      }
    }
  }

  void evolveEnvelope(const float &sampleTime) {
    switch (stage) {
    case STAGE_ATTACK: {
      envState += sampleTime / attackTime;
      env = std::min(envState, 1.f);
      break;
    }
    case STAGE_RELEASE: {
      envState -= sampleTime / releaseTime;
      env = std::max(0.f, envState);
      break;
    }
    case STAGE_OFF: {
      env = 0.0f;
      break;
    }
    case STAGE_SUSTAIN: {
      break;
    }
    }
  }

  void process(const float &sampleTime, const bool held) {
    processTransition(held);
    evolveEnvelope(sampleTime);
  }
};

struct ASDEnvelope : Envelope {

  Stage stage = STAGE_OFF;
  float envState = 0.f;
  float attackTime = 0.1f, releaseTime = 0.1f, sustain = 1.f;

  ASDEnvelope() {};

  void retrigger() {
    stage = STAGE_ATTACK;
    env = envState = 0.f;
  }

  void processTransition(const bool held) {
    if (stage == STAGE_ATTACK) {
      if (envState >= sustain) {
        eoa = 1.f;
        eor = 0.f;
        env = envState = sustain;
        stage = STAGE_SUSTAIN;
      }
    } else if (stage == STAGE_SUSTAIN) {
      if (!held) {
        stage = STAGE_RELEASE;
      }
    } else if (stage == STAGE_RELEASE) {
      if (envState <= 0.f) {
        eoa = 0.f;
        eor = 1.f;
        stage = STAGE_OFF;
        env = envState = 0.f;
      }
    }
  }

  void evolveEnvelope(const float &sampleTime) {
    switch (stage) {
    case STAGE_ATTACK: {
      envState += sampleTime / attackTime;
      env = std::min(envState, 1.f);
      break;
    }
    case STAGE_RELEASE: {
      envState -= sampleTime / releaseTime;
      env = std::max(0.f, envState);
      break;
    }
    case STAGE_SUSTAIN: {

      break;
    }
    case STAGE_OFF: {
      env = 0.0f;
      break;
    }
    }
  }

  void process(const float &sampleTime, const bool held) {
    processTransition(held);
    evolveEnvelope(sampleTime);
  }
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_ENVELOPE : Module {
  enum PARAM_IDS {
    ATK1_PARAM,
    ATK2_PARAM,
    ATK3_PARAM,
    ATK4_PARAM,
    REL3_PARAM,
    REL4_PARAM,
    SUS2_PARAM,
    SUS_PARAM,
    REL1_PARAM,
    REL2_PARAM,
    NUM_PARAMS
  };
  enum INPUT_IDS { TRIGGER1_INPUT, TRIGGER2_INPUT, TRIGGER3_INPUT, TRIGGER4_INPUT, NUM_INPUTS };
  enum OUTPUT_IDS {
    OUT1,
    OUT2,
    OUT3,
    OUT4,
    EOA1,
    EOA2,
    EOA3,
    EOA4,
    EOR1,
    EOR2,
    EOR3,
    EOR4,
    NUM_OUTPUTS
  };

  dsp::SchmittTrigger gateTrigger1, gateTrigger2, gateTrigger3, gateTrigger4;

  KI1H_ENVELOPE();
  void process(const ProcessArgs &args) override;
  static constexpr float minStageTime = 0.003f; // in seconds
  static constexpr float maxStageTime = 10.f;   // in seconds

  static float convertCVToTimeInSeconds(float cv) {
    return minStageTime * std::pow(maxStageTime / minStageTime, cv);
  }

private:
  ADEnvelope ad1, ad2;
  ASDEnvelope asd1, asd2;
  float CV_SCALE = 10.f;
};

// ============================================================================
// WIDGET DEFINITION
// ============================================================================
struct KI1H_ENVELOPEWidget : ModuleWidget {
  KI1H_ENVELOPEWidget(KI1H_ENVELOPE *module);
};

// ============================================================================
// PROCESS METHOD
// ============================================================================

// void ADEnvelope::process() {};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_ENVELOPE::KI1H_ENVELOPE() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
  configParam(ATK1_PARAM, 0.1f, 1.f, 0.1f, "AD1 Attack");
  configParam(ATK2_PARAM, 0.1f, 1.f, 0.1f, "ASD1 Attack");
  configParam(ATK3_PARAM, 0.1f, 1.f, 0.1f, "AD2 Attack");
  configParam(ATK4_PARAM, 0.1f, 1.f, 0.1f, "ASD2 Attack");
  configParam(REL1_PARAM, 0.1f, 1.f, 0.1f, "AD1 Release");
  configParam(REL2_PARAM, 0.1f, 1.f, 0.1f, "ASD1 Release");
  configParam(REL3_PARAM, 0.1f, 1.f, 0.1f, "AD2 Release");
  configParam(REL4_PARAM, 0.1f, 1.f, 0.1f, "ASD2 Release");
  configParam(SUS_PARAM, 0.1f, 1.f, 0.1f, "Sustain");
  configParam(SUS2_PARAM, 0.1f, 1.f, 0.1f, "Sustain2");
  configInput(TRIGGER1_INPUT, "AD1 Trigger");
  configInput(TRIGGER2_INPUT, "ASD1 Trigger");
  configInput(TRIGGER3_INPUT, "AD2 Trigger");
  configInput(TRIGGER4_INPUT, "ASD2 Trigger");

  // configInput(ATK_CV, "Attack CV");
  // configInput(REL_CV, "Release CV");
  configOutput(EOA1, "AD1 End of Attack");
  configOutput(EOA2, "ASD1 End of Attack");
  configOutput(EOA3, "AD2 End of Attack");
  configOutput(EOA4, "ASD2 End of Attack");
  configOutput(EOR1, "AD1 End of Release");
  configOutput(EOR2, "ASD1 End of Release");
  configOutput(EOR3, "AD2 End of Release");
  configOutput(EOR4, "ASD2 End of Release");
  configOutput(OUT1, "AD1 Output");
  configOutput(OUT2, "ASD1 Output");
  configOutput(OUT3, "AD2 Output");
  configOutput(OUT4, "ASD2 Output");
};

// ============================================================================
// Envelope - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_ENVELOPE::process(const ProcessArgs &args) {
  const float atk1Lvl = clamp(params[ATK1_PARAM].getValue(), 0.f, 1.f);
  ad1.attackTime = convertCVToTimeInSeconds(atk1Lvl);
  const float rls1Lvl = clamp(params[REL1_PARAM].getValue(), 0.f, 1.f);
  ad1.releaseTime = convertCVToTimeInSeconds(rls1Lvl);
  const bool triggered1 = gateTrigger1.process(inputs[TRIGGER1_INPUT].getVoltage());
  const bool ad1held = gateTrigger1.isHigh();
  if (triggered1) {
    ad1.retrigger();
  }

  ad1.process(args.sampleTime, ad1held);

  outputs[OUT1].setVoltage(ad1.env * CV_SCALE);
  outputs[EOA1].setVoltage(ad1.eoa * CV_SCALE);
  outputs[EOR1].setVoltage(ad1.eor * CV_SCALE);

  const float atk2Lvl = clamp(params[ATK2_PARAM].getValue(), 0.f, 1.f);
  asd1.attackTime = convertCVToTimeInSeconds(atk2Lvl);
  const float susLvl = clamp(params[SUS_PARAM].getValue(), 0.f, 1.f);
  asd1.sustain = susLvl;
  const float rls2Lvl = clamp(params[REL2_PARAM].getValue(), 0.f, 1.f);
  asd1.releaseTime = convertCVToTimeInSeconds(rls2Lvl);
  const bool ADSR1 = !inputs[TRIGGER2_INPUT].isConnected();
  float asr1TrigPulse = 0.f;
  if (ADSR1) {
    asr1TrigPulse = outputs[EOA1].getVoltage();
  } else {
    asr1TrigPulse = inputs[TRIGGER2_INPUT].getVoltage();
  }

  const bool triggered2 = gateTrigger2.process(asr1TrigPulse);
  const bool held1 = gateTrigger2.isHigh();
  if (triggered2) {
    asd1.retrigger();
  }

  asd1.process(args.sampleTime, held1);

  float adsr1Volt = asd1.env;
  if (ADSR1) {
    if (ad1.env > adsr1Volt) {
      adsr1Volt = ad1.env;
    }
  }

  outputs[OUT2].setVoltage(adsr1Volt * CV_SCALE);
  outputs[EOA2].setVoltage(asd1.eoa * CV_SCALE);
  outputs[EOR2].setVoltage(asd1.eor * CV_SCALE);

  const float atk3Lvl = clamp(params[ATK3_PARAM].getValue(), 0.f, 1.f);
  ad2.attackTime = convertCVToTimeInSeconds(atk3Lvl);
  const float rls3Lvl = clamp(params[REL3_PARAM].getValue(), 0.f, 1.f);
  ad2.releaseTime = convertCVToTimeInSeconds(rls3Lvl);
  const bool triggered3 = gateTrigger3.process(inputs[TRIGGER3_INPUT].getVoltage());
  const bool ad2held = gateTrigger3.isHigh();
  if (triggered3) {
    ad2.retrigger();
  }

  ad2.process(args.sampleTime, ad2held);

  outputs[OUT3].setVoltage(ad2.env * CV_SCALE);
  outputs[EOA3].setVoltage(ad2.eoa * CV_SCALE);
  outputs[EOR3].setVoltage(ad2.eor * CV_SCALE);

  const float atk4Lvl = clamp(params[ATK4_PARAM].getValue(), 0.f, 1.f);
  asd2.attackTime = convertCVToTimeInSeconds(atk4Lvl);
  const float sus2Lvl = clamp(params[SUS2_PARAM].getValue(), 0.f, 1.f);
  asd2.sustain = sus2Lvl;
  const float rls4Lvl = clamp(params[REL4_PARAM].getValue(), 0.f, 1.f);
  asd2.releaseTime = convertCVToTimeInSeconds(rls4Lvl);
  const bool ADSR2 = !inputs[TRIGGER4_INPUT].isConnected();
  float asr2TrigPulse = 0.f;
  if (ADSR2) {
    asr2TrigPulse = outputs[EOA3].getVoltage();
  } else {
    asr2TrigPulse = inputs[TRIGGER4_INPUT].getVoltage();
  }

  const bool triggered4 = gateTrigger4.process(asr2TrigPulse);
  bool held2 = false;
  if (ADSR2) {
    held2 = gateTrigger3.isHigh();
  } else {
    held2 = gateTrigger4.isHigh();
  }

  if (triggered4) {
    asd2.retrigger();
  }

  asd2.process(args.sampleTime, held2);
  float adsr2Volt = asd2.env;
  if (ADSR2) {
    if (ad2.env > adsr2Volt) {
      adsr2Volt = ad2.env;
    }
  }

  outputs[OUT4].setVoltage(adsr2Volt * CV_SCALE);
  outputs[EOA4].setVoltage(asd2.eoa * CV_SCALE);
  outputs[EOR4].setVoltage(asd2.eor * CV_SCALE);
};

KI1H_ENVELOPEWidget::KI1H_ENVELOPEWidget(KI1H_ENVELOPE *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-ENVELOPE.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewBlack>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                               KI1H_ENVELOPE::ATK1_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                               KI1H_ENVELOPE::REL1_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                               KI1H_ENVELOPE::ATK2_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[3], ROWS[1])), module,
                                               KI1H_ENVELOPE::SUS_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[4], ROWS[1])), module,
                                               KI1H_ENVELOPE::REL2_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[2] + HALF_R / 2)), module,
                                           KI1H_ENVELOPE::TRIGGER1_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA1));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR1));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[2] + HALF_R / 2)), module,
                                             KI1H_ENVELOPE::OUT1));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[2] + HALF_R / 2)), module,
                                           KI1H_ENVELOPE::TRIGGER2_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA2));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR2));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[2] + HALF_R / 2)), module,
                                             KI1H_ENVELOPE::OUT2));

  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[0], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::ATK3_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[1], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::REL3_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[2], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::ATK4_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[3], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::SUS2_PARAM));
  addChild(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[4], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::REL4_PARAM));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                           KI1H_ENVELOPE::TRIGGER3_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA3));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR3));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                             KI1H_ENVELOPE::OUT3));
  addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                           KI1H_ENVELOPE::TRIGGER4_INPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA4));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR4));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4], ROWS[5])), module,
                                             KI1H_ENVELOPE::OUT4));
};

Model *modelKI1H_ENVELOPE = createModel<KI1H_ENVELOPE, KI1H_ENVELOPEWidget>("KI1H-ENVELOPE");
