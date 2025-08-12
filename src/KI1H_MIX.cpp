
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "plugin.hpp"
#include "system.hpp"
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
class Channel {
public:
  void process(float input, float cvIn, float pan, bool mixBypass);
  float getOutput() const {
    return output;
  };
  float getROutput() const {
    return rOut;
  };
  float getLOutput() const {
    return lOut;
  };

private:
  float output = 0.f;
  float rOut = 0.f;
  float lOut = 0.f;
};

// ============================================================================
// MIX CLASS DEFINITION
// ============================================================================
class Mix {
public:
  void process(std::array<float, 5> lefts, std::array<float, 5> rights);
  float getLeftOut() const {
    return leftOut;
  };
  float getRightOUt() const {
    return rightOut;
  };

private:
  float leftOut = 0.f;
  float rightOut = 0.f;
};

// ============================================================================
// MIX MODULE DEFINITION
// ============================================================================
struct KI1H_MIX : Module {
  enum PARAM_IDS { ATT1, ATT2, ATT3, ATT4, ATT5, PAN1, PAN2, PAN3, PAN4, PAN5, NUM_PARAMS };
  enum INPUT_IDS { CV1, CV2, CV3, CV4, CV5, IN1, IN2, IN3, IN4, IN5, NUM_INPUTS };
  enum OUTPUT_IDS { OUT1, OUT2, OUT3, OUT4, OUT5, LOUT, ROUT, NUM_OUTPUTS };

  KI1H_MIX();
  void process(const ProcessArgs &args) override;

private:
  Channel channels[5];
  Mix mix;
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

void Channel::process(float input, float cvIn, float pan, bool mixBypass) {
  float ampd = input * cvIn;
  output = softLimit(ampd);
  if (mixBypass == false) {
    rOut = output * pan;
    lOut = output * (1 - pan);
  }
};

// ============================================================================
// MIX PROCESS METHOD
// ============================================================================
void Mix::process(std::array<float, 5> lefts, std::array<float, 5> rights) {
  leftOut = softLimit(std::accumulate(lefts.begin(), lefts.end(), 0.0f));
  rightOut = softLimit(std::accumulate(rights.begin(), rights.end(), 0.0f));
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_MIX::KI1H_MIX() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 6 channels
  for (int i = 0; i < 5; i++) {
    configParam(ATT1 + i, -1.2f, 1.2f, 0.f, "Ch" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configParam(PAN1 + i, 0.f, 1.f, 0.5f, "Pan" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
  }
};

// ============================================================================
// CHANNELS - PARAMETER CONFIGURATION
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
    float pan = params[PAN1 + i].getValue();

    // Process channel with CV scaled attenuverter
    channels[i].process(input, attenuverter + (cv / CV_SCALE), pan,
                        outputs[OUT1 + i].isConnected());

    // Set output
    outputs[OUT1 + i].setVoltage(channels[i].getOutput());
  }

  std::array<float, 5> lefts = {channels[0].getLOutput(), channels[1].getLOutput(),
                                channels[2].getLOutput(), channels[3].getLOutput(),
                                channels[4].getLOutput()};
  std::array<float, 5> rights = {channels[0].getROutput(), channels[1].getROutput(),
                                 channels[2].getROutput(), channels[3].getROutput(),
                                 channels[4].getROutput()};
  mix.process(lefts, rights);

  // Set mix outputs
  outputs[LOUT].setVoltage(mix.getLeftOut());
  outputs[ROUT].setVoltage(mix.getRightOUt());
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
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(25, 30)), module, KI1H_MIX::LOUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(50, 30)), module, KI1H_MIX::ROUT));
  int x = 15;
  for (int i = 0; i < 5; i++) {
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(10 + (i * x), 40)), module,
                                                 KI1H_MIX::PAN1 + i));
    addOutput(
        createOutputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 50)), module, KI1H_MIX::OUT1 + i));
    addParam(createParamCentered<BefacoSlidePot>(mm2px(Vec(10 + (i * x), 75)), module,
                                                 KI1H_MIX::ATT1 + i));
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 100)), module, KI1H_MIX::CV1 + i));
    addInput(
        createInputCentered<PJ301MPort>(mm2px(Vec(10 + (i * x), 110)), module, KI1H_MIX::IN1 + i));
  };
};

Model *modelKI1H_MIX = createModel<KI1H_MIX, KI1H_MIXWidget>("KI1H-MIX");
