
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
class Filter {
public:
  void process();

private:
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_FILTER : Module {
  enum PARAM_IDS { NUM_PARAMS };
  enum INPUT_IDS { NUM_INPUTS };
  enum OUTPUT_IDS { NUM_OUTPUTS };

  KI1H_FILTER();
  void process(const ProcessArgs &args) override;

private:
};

// ============================================================================
// WIDGET DEFINITION
// ============================================================================
struct KI1H_FILTERWidget : ModuleWidget {
  KI1H_FILTERWidget(KI1H_FILTER *module);
};

// ============================================================================
// PROCESS METHOD
// ============================================================================

void Filter::process() {};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_FILTER::KI1H_FILTER() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
};

// ============================================================================
// Filter - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_FILTER::process(const ProcessArgs &args) {};

KI1H_FILTERWidget::KI1H_FILTERWidget(KI1H_FILTER *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-FILTER.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewBlack>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
};

Model *modelKI1H_FILTER = createModel<KI1H_FILTER, KI1H_FILTERWidget>("KI1H-FILTER");
