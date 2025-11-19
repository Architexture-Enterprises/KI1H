
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
struct Klass {
public:
  void process();

private:
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_template : Module {
  enum PARAM_IDS { NUM_PARAMS };
  enum INPUT_IDS { NUM_INPUTS };
  enum OUTPUT_IDS { NUM_OUTPUTS };

  KI1H_template();
  KI1H_template(const KI1H_template &) = default;
  KI1H_template(KI1H_template &&) = default;
  KI1H_template &operator=(const KI1H_template &) = default;
  KI1H_template &operator=(KI1H_template &&) = default;
  void process(const ProcessArgs &args) override;

private:
  float CV_SCALE = 5.f;
};

// ============================================================================
// WIDGET DEFINITION
// ============================================================================
struct KI1H_templateWidget : ModuleWidget {
  KI1H_templateWidget(KI1H_template *module);
};

// ============================================================================
// PROCESS METHOD
// ============================================================================

void Klass::process() {};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_template::KI1H_template() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
};

// ============================================================================
// Klass - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_template::process(const ProcessArgs &args) {};

KI1H_templateWidget::KI1H_templateWidget(KI1H_template *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-template.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewBlack>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
};

Model *modelKI1H_template = createModel<KI1H_template, KI1H_templateWidget>("KI1H-template");
