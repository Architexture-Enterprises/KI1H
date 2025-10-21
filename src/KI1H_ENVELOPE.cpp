
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
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
  float timeInCurrentStage = 0.f;
  float attackTime = 0.1f, releaseTime = 0.1f;

  ADEnvelope() {};

  void retrigger() {
    stage = STAGE_ATTACK;
    timeInCurrentStage = 0.f;
  }

  void processTransition() {
    if (stage == STAGE_ATTACK || stage == STAGE_SUSTAIN) {
      if (timeInCurrentStage > attackTime) {
        eoa = 1.f;
        eor = 0.f;
        timeInCurrentStage = 0.f;
        stage = STAGE_RELEASE;
      }
    } else if (stage == STAGE_RELEASE) {
      if (timeInCurrentStage >= releaseTime) {
        eoa = 0.f;
        eor = 1.f;
        stage = STAGE_OFF;
        timeInCurrentStage = 0.f;
      }
    }
  }

  void evolveEnvelope(const float &sampleTime) {
    switch (stage) {
    case STAGE_ATTACK: {
      timeInCurrentStage += sampleTime;
      env = std::min(timeInCurrentStage / attackTime, 1.f);
      break;
    }
    case STAGE_RELEASE: {
      timeInCurrentStage += sampleTime;
      env = std::min(1.f, timeInCurrentStage / releaseTime);
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

  void process(const float &sampleTime) {
    processTransition();
    evolveEnvelope(sampleTime);
  }
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_ENVELOPE : Module {
  enum PARAM_IDS { ATTACK_PARAM, RELEASE_PARAM, NUM_PARAMS };
  enum INPUT_IDS { TRIGGER_INPUT, NUM_INPUTS };
  enum OUTPUT_IDS { OUT, EOA, EOR, NUM_OUTPUTS };

  dsp::SchmittTrigger gateTrigger;

  KI1H_ENVELOPE();
  void process(const ProcessArgs &args) override;
  static constexpr float minStageTime = 0.003f; // in seconds
  static constexpr float maxStageTime = 10.f;   // in seconds

  static float convertCVToTimeInSeconds(float cv) {
    return minStageTime * std::pow(maxStageTime / minStageTime, cv);
  }

private:
  ADEnvelope ad1;
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
  configParam(ATTACK_PARAM, 0.1f, 1.f, 0.1f, "Attack");
  configParam(RELEASE_PARAM, 0.1f, 1.f, 0.1f, "Release");
  configInput(TRIGGER_INPUT, "Trigger");
  // configInput(ATTACK_CV, "Attack CV");
  // configInput(RELEASE_CV, "Release CV");
  configOutput(EOA, "End of Attack");
  configOutput(EOR, "End of Release");
  configOutput(OUT, "Output");
};

// ============================================================================
// Envelope - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_ENVELOPE::process(const ProcessArgs &args) {
  const float atkLvl = clamp(params[ATTACK_PARAM].getValue(), 0.f, 1.f);
  ad1.attackTime = convertCVToTimeInSeconds(atkLvl);
  const float rlsLvl = clamp(params[RELEASE_PARAM].getValue(), 0.f, 1.f);
  ad1.releaseTime = convertCVToTimeInSeconds(rlsLvl);
  const bool triggered = gateTrigger.process(inputs[TRIGGER_INPUT].getVoltage());

  if (triggered) {
    ad1.retrigger();
  }

  ad1.process(args.sampleTime);

  outputs[OUT].setVoltage(ad1.env * CV_SCALE);
  outputs[EOA].setVoltage(ad1.eoa * CV_SCALE);
  outputs[EOR].setVoltage(ad1.eor * CV_SCALE);
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
  addChild(createParam<BefacoSlidePot>(mm2px(Vec(COLUMNS[0], ROWS[0])), module,
                                       KI1H_ENVELOPE::ATTACK_PARAM));
  addChild(createParam<BefacoSlidePot>(mm2px(Vec(COLUMNS[1], ROWS[0])), module,
                                       KI1H_ENVELOPE::RELEASE_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                             KI1H_ENVELOPE::EOA));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                             KI1H_ENVELOPE::EOR));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[0], ROWS[2])), module,
                                             KI1H_ENVELOPE::OUT));
};

Model *modelKI1H_ENVELOPE = createModel<KI1H_ENVELOPE, KI1H_ENVELOPEWidget>("KI1H-ENVELOPE");
