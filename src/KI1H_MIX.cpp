#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "dsp.hpp"
#include "plugin.hpp"
#include <array>
#include <numeric>
#include <string>

// ============================================================================
// MIX CLASS DEFINITION
// ============================================================================
struct Mix {
  void process(const std::array<float, 5> &all);
  float getAllOut() const {
    return allOut;
  };
  float getLeftOut() const {
    return leftOut;
  };
  float getRightOut() const {
    return rightOut;
  };

  float allOut = 0.f;
  float leftOut = 0.f;
  float rightOut = 0.f;
};

// ============================================================================
// MIX MODULE DEFINITION
// ============================================================================
struct KI1H_MIX : Module {
  enum ParamIds { ATT1_PARAM, ATT2_PARAM, ATT3_PARAM, ATT4_PARAM, ATT5_PARAM, MIX1_PARAM, MIX2_PARAM, MIX3_PARAM, MIX4_PARAM, MIX5_PARAM, NUM_PARAMS };
  enum InputIds { CV1_INPUT, CV2_INPUT, CV3_INPUT, CV4_INPUT, CV5_INPUT, IN1_INPUT, IN2_INPUT, IN3_INPUT, IN4_INPUT, IN5_INPUT, NUM_INPUTS };
  enum OutputIds { OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT, OUT4_OUTPUT, OUT5_OUTPUT, ALL_OUTPUT, L_OUTPUT, R_OUTPUT, NUM_OUTPUTS };

  KI1H_MIX();
  void process(const ProcessArgs &args) override;

private:
  ki1h::Channel channels[5];
  Mix mix;
  static constexpr float CV_SCALE = 5.f;
};

// ============================================================================
// MIX WIDGET DEFINITION
// ============================================================================
struct KI1H_MIXWidget : ModuleWidget {
  KI1H_MIXWidget(KI1H_MIX *module);
};

// ============================================================================
// MIX PROCESS METHOD
// ============================================================================
void Mix::process(const std::array<float, 5> &all) {
  std::array<float, 2> evens;
  std::array<float, 3> odds;
  for (std::size_t i = 0; i < all.size(); i++) {
    if (i % 2 == 0)
      odds[i / 2] = all[i];
    else
      evens[i / 2] = all[i];
  }
  allOut = ki1h::softLimit(std::accumulate(all.begin(), all.end(), 0.0f));
  leftOut = ki1h::softLimit(std::accumulate(odds.begin(), odds.end(), 0.0f));
  rightOut = ki1h::softLimit(std::accumulate(evens.begin(), evens.end(), 0.0f));
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_MIX::KI1H_MIX() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 6 channels
  for (int i = 0; i < 5; i++) {
    configParam(ATT1_PARAM + i, -1.f, 1.f, 0.f, "Attenuverter" + std::to_string(i + 1), "%", 0.f, 100,
                0.f);
    configParam(MIX1_PARAM + i, -1.2f, 1.2f, 0.f, "Level" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configInput(CV1_INPUT + i, "CV" + std::to_string(i + 1));
    configInput(IN1_INPUT + i, "In" + std::to_string(i + 1));
    configOutput(OUT1_OUTPUT + i, "Out" + std::to_string(i + 1));
  }
  configOutput(ALL_OUTPUT, "All");
  configOutput(L_OUTPUT, "Odds");
  configOutput(R_OUTPUT, "Evens");
};

void KI1H_MIX::process(const ProcessArgs &args) {
  std::array<float, 5> all;
  // Process all 6 channels
  for (int i = 0; i < 5; i++) {
    // Get input signal and CV
    float input = inputs[IN1_INPUT + i].getVoltage();
    // Get attenuverter parameter value
    float attenuverter = params[MIX1_PARAM + i].getValue();
    float cv = 0.0f;

    if (inputs[CV1_INPUT + i].isConnected())
      cv = (inputs[CV1_INPUT + i].getVoltage() * params[ATT1_PARAM + i].getValue()) / CV_SCALE;

    // Process channel with CV scaled attenuverter
    channels[i].process(input, attenuverter + cv);

    // Set output
    float output = channels[i].getOutput();
    outputs[OUT1_OUTPUT + i].setVoltage(output);
    if (outputs[OUT1_OUTPUT + i].isConnected())
      all[i] = 0.f;
    else
      all[i] = output;
  }

  mix.process(all);

  // Set mix outputs
  outputs[L_OUTPUT].setVoltage(mix.getLeftOut());
  outputs[ALL_OUTPUT].setVoltage(mix.getAllOut());
  outputs[R_OUTPUT].setVoltage(mix.getRightOut());
};

KI1H_MIXWidget::KI1H_MIXWidget(KI1H_MIX *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-MIX.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);

  // ============================================================================
  // VCA - CONTROL KNOBS
  // ============================================================================
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[0])), module,
                                             KI1H_MIX::L_OUTPUT));
  addOutput(
      createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2], ROWS[0])), module, KI1H_MIX::ALL_OUTPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_MIX::R_OUTPUT));

  for (int i = 0; i < 5; i++) {
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[1] - HALF_R)), module,
                                               KI1H_MIX::OUT1_OUTPUT + i));
    addParam(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[i], ROWS[2])), module,
                                                 KI1H_MIX::MIX1_PARAM + i));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[i], ROWS[4] - HALF_R)), module,
                                                 KI1H_MIX::ATT1_PARAM + i));
    addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[i], ROWS[4] + (HALF_R / 2))),
                                               module, KI1H_MIX::CV1_INPUT + i));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[5])), module,
                                             KI1H_MIX::IN1_INPUT + i));
  };
};

Model *modelKI1H_MIX = createModel<KI1H_MIX, KI1H_MIXWidget>("KI1H-MIX");
