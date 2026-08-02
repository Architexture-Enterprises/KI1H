#include "componentlibrary.hpp"
#include "helpers.hpp"
#include "dsp.hpp"
#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>

// ============================================================================
// VCA CLASS DEFINITION
// ============================================================================
struct VCA {
  void process(const std::array<float, 5> &channels, const std::array<float, 5> &pans);
  float getLeftOut() const {
    return leftOut;
  };
  float getRightOut() const {
    return rightOut;
  };

  float leftOut = 0.f;
  float rightOut = 0.f;
};

// ============================================================================
// VCA MODULE DEFINITION
// ============================================================================
struct KI1H_VCA : Module {
  enum ParamIds {
    PAN1_PARAM,
    PAN2_PARAM,
    PAN3_PARAM,
    PAN4_PARAM,
    PAN5_PARAM,
    MIX1_PARAM,
    MIX2_PARAM,
    MIX3_PARAM,
    MIX4_PARAM,
    MIX5_PARAM,
    PAN_CV1_PARAM,
    PAN_CV2_PARAM,
    NUM_PARAMS
  };
  enum InputIds { CV1_INPUT, CV2_INPUT, CV3_INPUT, CV4_INPUT, CV5_INPUT, IN1_INPUT, IN2_INPUT, IN3_INPUT, IN4_INPUT, IN5_INPUT, NUM_INPUTS };
  enum OutputIds { OUT1_OUTPUT, OUT2_OUTPUT, OUT3_OUTPUT, OUT4_OUTPUT, OUT5_OUTPUT, L_OUTPUT, R_OUTPUT, NUM_OUTPUTS };

  KI1H_VCA();
  void process(const ProcessArgs &args) override;

private:
  ki1h::Channel channels[5];
  VCA mix;
};

// ============================================================================
// VCA WIDGET DEFINITION
// ============================================================================
struct KI1H_VCAWidget : ModuleWidget {
  KI1H_VCAWidget(KI1H_VCA *module);
};

// ============================================================================
// VCA PROCESS METHOD
// ============================================================================
void VCA::process(const std::array<float, 5> &channels, const std::array<float, 5> &pans) {
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

  leftOut = ki1h::softLimit(leftSum);
  rightOut = ki1h::softLimit(rightSum);
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_VCA::KI1H_VCA() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);

  // Configure parameters for all 5 channels
  for (int i = 0; i < 5; i++) {
    configParam(PAN1_PARAM + i, -1.f, 1.f, 0.f, "Pan" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configParam(MIX1_PARAM + i, 0.f, 1.f, 1.f, "Level" + std::to_string(i + 1), "%", 0.f, 100, 0.f);
    configInput(CV1_INPUT + i, "CV" + std::to_string(i + 1));
    configInput(IN1_INPUT + i, "In" + std::to_string(i + 1));
    configOutput(OUT1_OUTPUT + i, "Out" + std::to_string(i + 1));
  }
  auto panCv1Switch = configSwitch(PAN_CV1_PARAM, 0.f, 1.f, 0.f, "CV1 Mode", {"Vol", "Pan"});
  panCv1Switch->snapEnabled = true;
  auto panCv2Switch = configSwitch(PAN_CV2_PARAM, 0.f, 1.f, 0.f, "CV5 Mode", {"Vol", "Pan"});
  panCv2Switch->snapEnabled = true;
  configOutput(L_OUTPUT, "Left");
  configOutput(R_OUTPUT, "Right");
};

void KI1H_VCA::process(const ProcessArgs &args) {
  std::array<float, 5> channelOutputs;
  std::array<float, 5> panValues;

  // Process all 5 channels
  for (int i = 0; i < 5; i++) {
    // Get input signal
    float input = inputs[IN1_INPUT + i].getVoltage();

    // Get level parameter (0-1 range)
    float level = params[MIX1_PARAM + i].getValue();

    // Get pan parameter
    float pan = params[PAN1_PARAM + i].getValue();

    // Get CV (unipolar: 0-10V range)
    float cv = 0.f;
    if (inputs[CV1_INPUT + i].isConnected()) {
      cv = inputs[CV1_INPUT + i].getVoltage();
    }

    // Channels 1 and 5 have a switch selecting what their CV does; channels
    // 2-4 are always volume. Pick the mode first, then apply it once.
    // 0 = volume mode, 1 = panning mode.
    const int cvMode = (i == 0)   ? (int)params[PAN_CV1_PARAM].getValue()
                       : (i == 4) ? (int)params[PAN_CV2_PARAM].getValue()
                                  : 0;

    if (inputs[CV1_INPUT + i].isConnected()) {
      if (cvMode == 1) {
        // CV controls panning. Note this maps 0-5 V onto centre-to-hard-right
        // and clamps everything above 5 V; the "5V = center" comment this
        // replaced described a bipolar mapping the code never implemented.
        // Preserved as-is — changing it would change the sound.
        pan = std::max(-1.f, std::min(1.f, cv / 5.f));
      } else {
        // CV controls volume (0-10V -> 0-1 gain)
        level *= std::max(0.f, std::min(1.f, cv / 10.f));
      }
    }

    // Apply level as unipolar gain
    float gain = level;
    channels[i].process(input, gain);

    // Get channel output
    float output = channels[i].getOutput();
    outputs[OUT1_OUTPUT + i].setVoltage(output);

    // Store output for panning (only if individual output is not connected)
    if (outputs[OUT1_OUTPUT + i].isConnected())
      channelOutputs[i] = 0.f;
    else
      channelOutputs[i] = output;

    // Store pan value for this channel
    panValues[i] = pan;
  }

  // Process panning and sum to left/right outputs
  mix.process(channelOutputs, panValues);

  // Set left and right outputs
  outputs[L_OUTPUT].setVoltage(mix.getLeftOut());
  outputs[R_OUTPUT].setVoltage(mix.getRightOut());
};

KI1H_VCAWidget::KI1H_VCAWidget(KI1H_VCA *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-VCA.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);

  // ============================================================================
  // VCA - CONTROL KNOBS
  // ============================================================================
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[1] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::PAN_CV1_PARAM));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::PAN_CV2_PARAM));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[2] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::L_OUTPUT));
  addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[3] - HALF_C, ROWS[0])), module,
                                             KI1H_VCA::R_OUTPUT));

  for (int i = 0; i < 5; i++) {
    addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[1] - HALF_R)), module,
                                               KI1H_VCA::OUT1_OUTPUT + i));
    addParam(createParamCentered<BefacoSlidePot>(mm2px(Vec(COLUMNS[i], ROWS[2])), module,
                                                 KI1H_VCA::MIX1_PARAM + i));
    addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(COLUMNS[i], ROWS[4] - HALF_R)), module,
                                                 KI1H_VCA::PAN1_PARAM + i));
    addInput(createInputCentered<BananutBlack>(mm2px(Vec(COLUMNS[i], ROWS[4] + (HALF_R / 2))),
                                               module, KI1H_VCA::CV1_INPUT + i));
    addInput(createInputCentered<PJ301MPort>(mm2px(Vec(COLUMNS[i], ROWS[5])), module,
                                             KI1H_VCA::IN1_INPUT + i));
  };
};

Model *modelKI1H_VCA = createModel<KI1H_VCA, KI1H_VCAWidget>("KI1H-VCA");
