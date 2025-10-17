
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "plugin.hpp"

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// ============================================================================
// CLASS DEFINITION
// ============================================================================
struct Envelope {
  float env = 0.f;
};

struct ADEnvelope : Envelope {
  enum Stage { STAGE_OFF, STAGE_ATTACK, STAGE_RELEASE };

  Stage stage = STAGE_OFF;
  float releaseValue;
  float timeInCurrentStage = 0.f;
  float attackTime = 0.1, releaseTime = 0.1;

  ADEnvelope() {};

  void processTransition() {
    if (stage == STAGE_ATTACK) {
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
    case STAGE_OFF: {
      env = 0.f;
      break;
    }
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
  enum PARAM_IDS { NUM_PARAMS };
  enum INPUT_IDS { NUM_INPUTS };
  enum OUTPUT_IDS { NUM_OUTPUTS };

  KI1H_ENVELOPE();
  KI1H_ENVELOPE(const KI1H_ENVELOPE &) = default;
  KI1H_ENVELOPE(KI1H_ENVELOPE &&) = default;
  KI1H_ENVELOPE &operator=(const KI1H_ENVELOPE &) = default;
  KI1H_ENVELOPE &operator=(KI1H_ENVELOPE &&) = default;
  void process(const ProcessArgs &args) override;

private:
  float CV_SCALE = 5.f;
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
};

// ============================================================================
// Envelope - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_ENVELOPE::process(const ProcessArgs &args) {};

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
