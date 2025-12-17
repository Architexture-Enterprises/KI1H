
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "plugin.hpp"
#include <array>
#include <numeric>
#include <string>

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
float softLimit(float input) {
  if (fabs(input) > 5.2f) {
    float sign = (input >= 0) ? 1.0f : -1.0f;
    float excess = fabs(input) - 5.2f;
    return sign * (5.2f + excess * exp(-excess * 2.0f));
  } else {
    return input;
  }
}

// ============================================================================
// CHANNEL CLASS DEFINITION
// ============================================================================
struct Channel {
  void process(float input, float cvIn);
  float getOutput() const {
    return output;
  };

  float output = 0.f;
};

// ============================================================================
// VCA CLASS DEFINITION
// ============================================================================
struct VCA {
  void process(std::array<float, 5> all);
  float getLeftOut() const {
    return leftOut;
  };
  float getRightOUt() const {
    return rightOut;
  };

  float leftOut = 0.f;
  float rightOut = 0.f;
};

// ============================================================================
// VCA MODULE DEFINITION
// ============================================================================
struct KI1H_VCA : Module {
  enum PARAM_IDS { PAN1, PAN2, PAN3, PAN4, PAN5, MIX1, MIX2, MIX3, MIX4, MIX5, NUM_PARAMS };
  enum INPUT_IDS { CV1, CV2, CV3, CV4, CV5, IN1, IN2, IN3, IN4, IN5, NUM_INPUTS };
  enum OUTPUT_IDS { OUT1, OUT2, OUT3, OUT4, OUT5, LOUT, ROUT, NUM_OUTPUTS };

  KI1H_VCA();
  void process(const ProcessArgs &args) override;

private:
  Channel channels[5];
  VCA mix;
  float CV_SCALE = 5.f;
};

// ============================================================================
// VCA WIDGET DEFINITION
// ============================================================================
struct KI1H_VCAWidget : ModuleWidget {
  KI1H_VCAWidget(KI1H_VCA *module);
};

// ============================================================================
// CHANNEL PROCESS METHOD
// ============================================================================

void Channel::process(float input, float cvIn) {
  float ampd = input * cvIn;
  output = softLimit(ampd);
};

// ============================================================================
// VCA PROCESS METHOD
// ============================================================================
void VCA::process(std::array<float, 5> all) {
  std::array<float, 2> evens;
  std::array<float, 3> odds;
  for (unsigned long i = 0; i < sizeof(all); i++) {
    if (i % 2 == 0)
      odds[i / 2] = all[i];
    else
      evens[i / 2] = all[i];
  }
  leftOut = softLimit(std::accumulate(odds.begin(), odds.end(), 0.0f));
  rightOut = softLimit(std::accumulate(evens.begin(), evens.end(), 0.0f));
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_VCA::KI1H_VCA() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 6 channels
  for (int i = 0; i < 5; i++) {
    configParam(PAN1 + i, -1.f, 1.f, 0.f, "Pan" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configParam(MIX1 + i, -1.2f, 1.2f, 0.f, "Level" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configInput(CV1 + i, "CV" + std::to_string(i + 1));
    configInput(IN1 + i, "In" + std::to_string(i + 1));
    configOutput(OUT1 + i, "Out" + std::to_string(i + 1));
  }
  configOutput(LOUT, "Left");
  configOutput(ROUT, "Right");
};

// ============================================================================
// CHANNELS - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_VCA::process(const ProcessArgs &args) {
  std::array<float, 5> all;
  // Process all 6 channels
  for (int i = 0; i < 5; i++) {
    // Get input signal and CV
    float input = inputs[IN1 + i].getVoltage();
    float cv = 1;
    if (inputs[CV1 + i].isConnected())
      cv = inputs[CV1 + i].getVoltage() * params[PAN1 + i].getValue();

    // Get attenuverter parameter value
    float attenuverter = params[MIX1 + i].getValue();

    // Process channel with CV scaled attenuverter
    channels[i].process(input, attenuverter + (cv / CV_SCALE));

    // Set output
    float output = channels[i].getOutput();
    outputs[OUT1 + i].setVoltage(output);
    if (outputs[CV1 + i].isConnected())
      all[i] = 0.f;
    else
      all[i] = output;
  }

  mix.process(all);

  // Set mix outputs
  outputs[LOUT].setVoltage(mix.getLeftOut());
  outputs[ROUT].setVoltage(mix.getRightOUt());
};

KI1H_VCAWidget::KI1H_VCAWidget(KI1H_VCA *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-VCA.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewBlack>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  // ============================================================================
  // VCA - CONTROL KNOBS
  // ============================================================================
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::LOUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::ROUT));

  for (int i = 0; i < 5; i++) {
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[1] - HALF_R)), module,
                                               KI1H_VCA::OUT1 + i));
    addParam(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[i], ROWS[2])), module,
                                                 KI1H_VCA::MIX1 + i));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[i], ROWS[4] - HALF_R)), module,
                                                 KI1H_VCA::PAN1 + i));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[4] + (HALF_R / 2))), module,
                                             KI1H_VCA::CV1 + i));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[5])), module,
                                             KI1H_VCA::IN1 + i));
  };
};

Model *modelKI1H_VCA = createModel<KI1H_VCA, KI1H_VCAWidget>("KI1H-VCA");
