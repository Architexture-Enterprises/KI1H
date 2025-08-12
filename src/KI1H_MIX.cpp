
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
#include "plugin.hpp"
#include "system.hpp"
#include <string>

// ============================================================================
// MIX CLASS DEFINITION
// ============================================================================
class Channel {
public:
  void process(float input, float cvIn);
  float getOutput() const {
    return output;
  };

private:
  float output = 0.f;
};

// ============================================================================
// MIX MODULE DEFINITION
// ============================================================================
struct KI1H_MIX : Module {
  enum PARAM_IDS { ATT1, ATT2, ATT3, ATT4, ATT5, NUM_PARAMS };
  enum INPUT_IDS { CV1, CV2, CV3, CV4, CV5, IN1, IN2, IN3, IN4, IN5, NUM_INPUTS };
  enum OUTPUT_IDS { OUT1, OUT2, OUT3, OUT4, OUT5, NUM_OUTPUTS };

  KI1H_MIX();
  void process(const ProcessArgs &args) override;

private:
  Channel channels[5];
  float CV_SCALE = 5.f;
};

// ============================================================================
// MIX WIDGET DEFINITION
// ============================================================================
struct KI1H_MIXWidget : ModuleWidget {
  KI1H_MIXWidget(KI1H_MIX *module);
};

// ============================================================================
// CHANNEL PROCESS METHOD
// ============================================================================

void Channel::process(float input, float cvIn) {
  float ampd = input * cvIn;

  if (fabs(ampd) > 5.2f) {
    float sign = (ampd >= 0) ? 1.0f : -1.0f;
    float excess = fabs(ampd) - 5.2f;
    // Exponential decay - gets closer to 5V as excess increases
    output = sign * (5.2f + excess * exp(-excess * 2.0f));
  } else {
    output = ampd;
  }
}

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_MIX::KI1H_MIX() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 6 channels
  for (int i = 0; i < 5; i++) {
    configParam(ATT1 + i, -1.2f, 1.2f, 0.f, "Ch" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
  }
};
// ============================================================================
// MIX 1 - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_MIX::process(const ProcessArgs &args) {
  // Process all 6 channels
  for (int i = 0; i < 5; i++) {
    // Get input signal and CV
    float input = inputs[IN1 + i].getVoltage();
    float cv = 1;
    if (inputs[CV1 + i].isConnected())
      cv = inputs[CV1 + i].getVoltage();

    // Get attenuverter parameter value
    float attenuverter = params[ATT1 + i].getValue();

    // Process channel with CV scaled attenuverter
    channels[i].process(input, attenuverter + (cv / CV_SCALE));

    // Set output
    outputs[OUT1 + i].setVoltage(channels[i].getOutput());
  }
};

KI1H_MIXWidget::KI1H_MIXWidget(KI1H_MIX *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-MIX.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  addChild(createWidget<ScrewSilver>(
      Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  // ============================================================================
  // VCA - CONTROL KNOBS
  // ============================================================================
  int x = 15;
  for (int i = 0; i < 5; i++) {
    addParam(createParamCentered<BefacoSlidePot>(mm2px(Vec(10 + (i * x), 55)), module,
                                                 KI1H_MIX::ATT1 + i));
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 80)), module, KI1H_MIX::CV1 + i));
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 90)), module, KI1H_MIX::IN1 + i));
    addOutput(
        createOutputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 30)), module, KI1H_MIX::OUT1 + i));
  };
};

Model *modelKI1H_MIX = createModel<KI1H_MIX, KI1H_MIXWidget>("KI1H-MIX");
