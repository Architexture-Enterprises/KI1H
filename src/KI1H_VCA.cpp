
// ============================================================================
// INCLUDES & GLOBAL VARIABLES
// ============================================================================
#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
namespace {
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
} // namespace

// ============================================================================
// VCA CLASS DEFINITION
// ============================================================================
struct VCA {
  void process(std::array<float, 5> channels, std::array<float, 5> pans);
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
  enum PARAM_IDS {
    PAN1,
    PAN2,
    PAN3,
    PAN4,
    PAN5,
    MIX1,
    MIX2,
    MIX3,
    MIX4,
    MIX5,
    PAN_CV1,
    PAN_CV2,
    NUM_PARAMS
  };
  enum INPUT_IDS { CV1, CV2, CV3, CV4, CV5, IN1, IN2, IN3, IN4, IN5, NUM_INPUTS };
  enum OUTPUT_IDS { OUT1, OUT2, OUT3, OUT4, OUT5, LOUT, ROUT, NUM_OUTPUTS };

  KI1H_VCA();
  void process(const ProcessArgs &args) override;

private:
  Channel channels[5];
  VCA mix;
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
  // CV is unipolar (0-1 range), acts as gain
  float ampd = input * cvIn;
  output = softLimit(ampd);
};

// ============================================================================
// VCA PROCESS METHOD
// ============================================================================
void VCA::process(std::array<float, 5> channels, std::array<float, 5> pans) {
  float leftSum = 0.f;
  float rightSum = 0.f;

  // Distribute each channel to left/right based on panning
  // Pan: -1 = full left, 0 = center, +1 = full right
  for (int i = 0; i < 5; i++) {
    float pan = pans[i];
    // Clamp pan to [-1, 1] range
    pan = std::max(-1.f, std::min(1.f, pan));

    // Calculate left and right gains (linear panning)
    // When pan = -1: left = 1, right = 0
    // When pan = 0: left = 0.5, right = 0.5
    // When pan = +1: left = 0, right = 1
    float leftGain = (1.f - pan) * 0.5f;
    float rightGain = (1.f + pan) * 0.5f;

    leftSum += channels[i] * leftGain;
    rightSum += channels[i] * rightGain;
  }

  leftOut = softLimit(leftSum);
  rightOut = softLimit(rightSum);
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_VCA::KI1H_VCA() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 5 channels
  for (int i = 0; i < 5; i++) {
    configParam(PAN1 + i, -1.f, 1.f, 0.f, "Pan" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configParam(MIX1 + i, 0.f, 1.f, 1.f, "Level" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configInput(CV1 + i, "CV" + std::to_string(i + 1));
    configInput(IN1 + i, "In" + std::to_string(i + 1));
    configOutput(OUT1 + i, "Out" + std::to_string(i + 1));
  }
  auto panCv1Switch = configSwitch(PAN_CV1, 0.f, 1.f, 0.f, "CV1 Mode", {"Vol", "Pan"});
  panCv1Switch->snapEnabled = true;
  auto panCv2Switch = configSwitch(PAN_CV2, 0.f, 1.f, 0.f, "CV5 Mode", {"Vol", "Pan"});
  panCv2Switch->snapEnabled = true;
  configOutput(LOUT, "Left");
  configOutput(ROUT, "Right");
};

// ============================================================================
// CHANNELS - PARAMETER CONFIGURATION
// ============================================================================

void KI1H_VCA::process(const ProcessArgs &args) {
  std::array<float, 5> channelOutputs;
  std::array<float, 5> panValues;

  // Process all 5 channels
  for (int i = 0; i < 5; i++) {
    // Get input signal
    float input = inputs[IN1 + i].getVoltage();

    // Get level parameter (0-1 range)
    float level = params[MIX1 + i].getValue();

    // Get pan parameter
    float pan = params[PAN1 + i].getValue();

    // Get CV (unipolar: 0-10V range)
    float cv = 0.f;
    if (inputs[CV1 + i].isConnected()) {
      cv = inputs[CV1 + i].getVoltage();
    }

    // Check PAN_CV switches for channel 1 and channel 5
    if (i == 0) {
      // Channel 1: Check PAN_CV1 switch (0 = volume mode, 1 = panning mode)
      int panCv1Mode = (int)params[PAN_CV1].getValue();
      if (panCv1Mode == 1 && inputs[CV1 + i].isConnected()) {
        // CV controls panning (0-10V -> -1 to +1 pan, 5V = center)
        float cvPan = (cv / 5.f); // Map 0-10V to -1 to +1
        pan = std::max(-1.f, std::min(1.f, cvPan));
      } else if (panCv1Mode == 0 && inputs[CV1 + i].isConnected()) {
        // CV controls volume (0-10V -> 0-1 gain)
        float cvGain = std::max(0.f, std::min(1.f, cv / 10.f));
        level *= cvGain;
      }
    } else if (i == 4) {
      // Channel 5: Check PAN_CV2 switch (0 = volume mode, 1 = panning mode)
      int panCv2Mode = (int)params[PAN_CV2].getValue();
      if (panCv2Mode == 1 && inputs[CV1 + i].isConnected()) {
        // CV controls panning (0-10V -> -1 to +1 pan, 5V = center)
        float cvPan = (cv / 5.f); // Map 0-10V to -1 to +1
        pan = std::max(-1.f, std::min(1.f, cvPan));
      } else if (panCv2Mode == 0 && inputs[CV1 + i].isConnected()) {
        // CV controls volume (0-10V -> 0-1 gain)
        float cvGain = std::max(0.f, std::min(1.f, cv / 10.f));
        level *= cvGain;
      }
    } else {
      // Channels 2, 3, 4: CV always controls volume
      if (inputs[CV1 + i].isConnected()) {
        float cvGain = std::max(0.f, std::min(1.f, cv / 10.f));
        level *= cvGain;
      }
    }

    // Apply level as unipolar gain
    float gain = level;
    channels[i].process(input, gain);

    // Get channel output
    float output = channels[i].getOutput();
    outputs[OUT1 + i].setVoltage(output);

    // Store output for panning (only if individual output is not connected)
    if (outputs[OUT1 + i].isConnected())
      channelOutputs[i] = 0.f;
    else
      channelOutputs[i] = output;

    // Store pan value for this channel
    panValues[i] = pan;
  }

  // Process panning and sum to left/right outputs
  mix.process(channelOutputs, panValues);

  // Set left and right outputs
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
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::PAN_CV1));
  addParam(createParamCentered<BefacoSwitch>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::PAN_CV2));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::LOUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3] - HALF_C, ROWS[0])), module,
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
