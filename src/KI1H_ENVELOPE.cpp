
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
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
};

struct ADEnvelope : Envelope {

  Stage stage = STAGE_OFF;
  float releaseValue;
  float timeInCurrentStage = 0.f;
  float attackTime = 0.1, releaseTime = 0.1;

  ADEnvelope() {};

  void processTransition() {
    if (stage == STAGE_ATTACK || stage == STAGE_SUSTAIN) {
      timeInCurrentStage = 0.f;
      stage = STAGE_RELEASE;
      releaseValue = env;
    } else if (stage == STAGE_RELEASE) {
      if (timeInCurrentStage > releaseTime) {
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
    case STAGE_OFF:
    case STAGE_SUSTAIN: {
      break;
    }
    }
  }

  void process(const float &sampleTime) {
    evolveEnvelope(sampleTime);
  }
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_ENVELOPE : Module {
  enum PARAM_IDS { ATTACK_PARAM, RELEASE_PARAM, NUM_PARAMS };
  enum INPUT_IDS { TRIGGER_INPUT, ATTACK_CV, RELEASE_CV, NUM_INPUTS };
  enum OUTPUT_IDS { OUT, EOA, EOR, NUM_OUTPUTS };

  dsp::SchmittTrigger gateTrigger;

  KI1H_ENVELOPE();
  void process(const ProcessArgs &args) override;

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
  configOutput(EOA, "End of Attack");
  configOutput(EOR, "End of Release");
  configOutput(OUT, "Output");
};

// ============================================================================
// Envelope - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_ENVELOPE::process(const ProcessArgs &args) {
  const float atkLvl = clamp(params[ATTACK_PARAM].getValue(), 0.f, 1.f);
  const float rlsLvl = clamp(params[RELEASE_PARAM].getValue(), 0.f, 1.f);
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
};

Model *modelKI1H_ENVELOPE = createModel<KI1H_ENVELOPE, KI1H_ENVELOPEWidget>("KI1H-ENVELOPE");
