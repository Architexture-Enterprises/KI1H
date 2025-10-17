
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
public:
  void process();

private:
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

void Envelope::process() {};

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
