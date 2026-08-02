
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

  Stage stage = STAGE_OFF;
  float envState = 0.f;
  float attackTime = 0.1f, releaseTime = 0.1f;

  void retrigger() {
    eoa = 0.f;
    eor = 1.f;
    stage = STAGE_ATTACK;
    env = envState = 0.f;
  }

  /** Advances envState for the current stage. Shared by both subclasses; the
  only stage that behaves differently between them is the transition logic,
  which is processTransition's job. */
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
      // Held at its current level; only ASDEnvelope ever reaches this.
      break;
    }
    case STAGE_OFF: {
      env = 0.0f;
      break;
    }
    }
  }
};

/** Attack then straight into release. Never reaches STAGE_SUSTAIN. */
struct ADEnvelope : Envelope {

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

  void process(const float &sampleTime, const bool held) {
    processTransition(held);
    evolveEnvelope(sampleTime);
  }
};

/** Attack to a sustain level, hold there while gated (when asr is set), then
release. The sustain stage is the only behavioural difference from AD. */
struct ASDEnvelope : Envelope {

  float sustain = 1.f;

  void processTransition(const bool asr, const bool held) {
    if (stage == STAGE_ATTACK) {
      eoa = 1.f;
      eor = 0.f;
      if (envState >= sustain) {
        env = envState = sustain;
        if (asr) {
          stage = STAGE_SUSTAIN;
        } else {
          stage = STAGE_RELEASE;
        }
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

  void process(const float &sampleTime, const bool sus, const bool held) {
    processTransition(sus, held);
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
    ASR1_SWITCH,
    ASR2_SWITCH,
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

  // [0]=AD1 [1]=ASD1 [2]=AD2 [3]=ASD2
  dsp::SchmittTrigger gateTrigger[4];

  KI1H_ENVELOPE();
  void process(const ProcessArgs &args) override;
  static constexpr float minStageTime = 0.003f; // in seconds
  static constexpr float maxStageTime = 10.f;   // in seconds

  static float convertCVToTimeInSeconds(float cv) {
    return minStageTime * std::pow(maxStageTime / minStageTime, cv);
  }

private:
  ADEnvelope ad[2];
  ASDEnvelope asd[2];
  static constexpr float CV_SCALE = 10.f;
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
  configParam(ASR1_SWITCH, 0.f, 1.f, 0.f, "AR/ASR Switch1");
  configParam(ASR2_SWITCH, 0.f, 1.f, 0.f, "AR/ASR Switch2");
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
  // ATK, TRIGGER, OUT, EOA and EOR all stride by 2 between the two AD/ASD
  // pairs, so those index arithmetically off the first member of each enum.
  //
  // The release and sustain params do not: they were declared out of order
  // (REL3, REL4, SUS2, SUS, REL1, REL2). Renumbering them is not an option —
  // Rack serializes params by index, so it would silently break every saved
  // patch — so look those two up instead.
  static const int adRelParam[2] = {REL1_PARAM, REL3_PARAM};
  static const int asdRelParam[2] = {REL2_PARAM, REL4_PARAM};
  static const int asdSusParam[2] = {SUS_PARAM, SUS2_PARAM};

  for (int i = 0; i < 2; i++) {
    const int adIdx = 2 * i;      // AD1, then AD2
    const int asdIdx = 2 * i + 1; // ASD1, then ASD2

    // Each AD/ASD pair is self-contained: within a pair the AD's end-of-attack
    // normals into the ASD's trigger, but nothing crosses between the pairs. So
    // a pair whose six outputs are all empty can be skipped whole, which also
    // skips its four convertCVToTimeInSeconds calls (a std::pow each).
    const bool pairLive =
        outputs[OUT1 + adIdx].isConnected() || outputs[OUT1 + asdIdx].isConnected() ||
        outputs[EOA1 + adIdx].isConnected() || outputs[EOA1 + asdIdx].isConnected() ||
        outputs[EOR1 + adIdx].isConnected() || outputs[EOR1 + asdIdx].isConnected();
    if (!pairLive)
      continue;

    // ========================================================================
    // AD STAGE
    // ========================================================================
    ad[i].attackTime =
        convertCVToTimeInSeconds(clamp(params[ATK1_PARAM + adIdx].getValue(), 0.f, 1.f));
    ad[i].releaseTime =
        convertCVToTimeInSeconds(clamp(params[adRelParam[i]].getValue(), 0.f, 1.f));

    const bool adTriggered =
        gateTrigger[adIdx].process(inputs[TRIGGER1_INPUT + adIdx].getVoltage());
    const bool adHeld = gateTrigger[adIdx].isHigh();
    if (adTriggered)
      ad[i].retrigger();

    ad[i].process(args.sampleTime, adHeld);

    outputs[OUT1 + adIdx].setVoltage(ad[i].env * CV_SCALE);
    outputs[EOA1 + adIdx].setVoltage(ad[i].eoa * CV_SCALE);
    outputs[EOR1 + adIdx].setVoltage(ad[i].eor * CV_SCALE);

    // ========================================================================
    // ASD STAGE
    // ========================================================================
    asd[i].attackTime =
        convertCVToTimeInSeconds(clamp(params[ATK1_PARAM + asdIdx].getValue(), 0.f, 1.f));
    asd[i].sustain = clamp(params[asdSusParam[i]].getValue(), 0.f, 1.f);
    asd[i].releaseTime =
        convertCVToTimeInSeconds(clamp(params[asdRelParam[i]].getValue(), 0.f, 1.f));

    // With nothing patched into the ASD's own trigger, the pair acts as one
    // AHDSR: the ASD is fired by the AD's end-of-attack.
    const bool chained = !inputs[TRIGGER1_INPUT + asdIdx].isConnected();
    const float asdTrigPulse = chained ? ad[i].eoa * CV_SCALE
                                       : inputs[TRIGGER1_INPUT + asdIdx].getVoltage();

    const bool asdTriggered = gateTrigger[asdIdx].process(asdTrigPulse);

    // When chained, sustain has to be held by the AD stage's real gate. The
    // signal driving gateTrigger[asdIdx] is end-of-attack, which is a pulse,
    // not a gate, so holding off it would barely sustain at all.
    const bool asdHeld = chained ? gateTrigger[adIdx].isHigh() : gateTrigger[asdIdx].isHigh();
    const bool asr = params[ASR1_SWITCH + i].getValue() > 0.f;
    if (asdTriggered)
      asd[i].retrigger();

    asd[i].process(args.sampleTime, asr, asdHeld);

    // In chained mode the pair's output is the louder of the two stages, so
    // the AD's attack is not swallowed by the ASD still being at zero.
    float asdVolt = asd[i].env;
    if (chained && ad[i].env > asdVolt)
      asdVolt = ad[i].env;

    outputs[OUT1 + asdIdx].setVoltage(asdVolt * CV_SCALE);
    outputs[EOA1 + asdIdx].setVoltage(asd[i].eoa * CV_SCALE);
    outputs[EOR1 + asdIdx].setVoltage(asd[i].eor * CV_SCALE);
  }
};

KI1H_ENVELOPEWidget::KI1H_ENVELOPEWidget(KI1H_ENVELOPE *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-ENVELOPE.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);
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
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::TRIGGER1_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA1));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR1));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[1], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::OUT1));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[2], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::TRIGGER2_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA2));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[3], ROWS[2] + HALF_R / 2)), module,
                                             KI1H_ENVELOPE::ASR1_SWITCH));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR2));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[4], ROWS[2] + HALF_R / 2)), module,
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
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                              KI1H_ENVELOPE::TRIGGER3_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA3));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR3));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                              KI1H_ENVELOPE::OUT3));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                              KI1H_ENVELOPE::TRIGGER4_INPUT));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[3], ROWS[5])), module,
                                             KI1H_ENVELOPE::ASR2_SWITCH));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA4));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR4));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[4], ROWS[5])), module,
                                              KI1H_ENVELOPE::OUT4));
};

Model *modelKI1H_ENVELOPE = createModel<KI1H_ENVELOPE, KI1H_ENVELOPEWidget>("KI1H-ENVELOPE");
