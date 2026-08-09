#pragma once
#include <array>
#include <rack.hpp>

using namespace rack;

// Declare the Plugin, defined in plugin.cpp
extern Plugin *pluginInstance;

// Declare each Model, defined in each module source file
extern Model *modelKI1H_VCO;
extern Model *modelKI1H_LFO;
extern Model *modelKI1H_MIX;
extern Model *modelKI1H_FILTER;
extern Model *modelKI1H_ENVELOPE;
extern Model *modelKI1H_KAOS;
extern Model *modelKI1H_VCA;

// UI Layout Constants - 6 rows with 18.8 spacing
constexpr float ROW_SPACING = 18.8f;
constexpr float HALF_R = 9.4f;
constexpr float ROW_START = 20.0f;
constexpr int NUM_ROWS = 6;

// 5 columns with 15 spacing
constexpr float COLUMN_SPACING = 15.f;
constexpr float HALF_C = 7.5f;
constexpr float COLUMN_START = 10.64f;
constexpr int NUM_COLUMNS = 5;

// getRowPosition / getColumnPosition take 1-based positions and exist only to
// build the arrays below. Widget code uses the arrays, which are 0-based:
// ROWS[0] is row 1, COLUMNS[0] is column 1.
constexpr float getRowPosition(int row) {
  return ROW_START + (row - 1) * ROW_SPACING;
}

constexpr std::array<float, NUM_ROWS> ROWS = {getRowPosition(1), getRowPosition(2),
                                              getRowPosition(3), getRowPosition(4),
                                              getRowPosition(5), getRowPosition(6)};

constexpr float getColumnPosition(int column) {
  return COLUMN_START + (column - 1) * COLUMN_SPACING;
}

constexpr std::array<float, NUM_COLUMNS> COLUMNS = {getColumnPosition(1), getColumnPosition(2),
                                                    getColumnPosition(3), getColumnPosition(4),
                                                    getColumnPosition(5)};

/** Adds the four corner screws every full-width KI1H panel carries.
Call after setPanel(), which is what gives box.size its final width. */
inline void addPanelScrews(ModuleWidget *w) {
  w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
  w->addChild(createWidget<ScrewBlack>(Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  w->addChild(createWidget<ScrewBlack>(
      Vec(w->box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
}

struct BananutOrange : app::SvgPort {
  BananutOrange() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/BananutOrange.svg")));
  }
};

struct BananutRed : app::SvgPort {
  BananutRed() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/BananutRed.svg")));
  }
};

struct BananutBlue : app::SvgPort {
  BananutBlue() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/BananutBlue.svg")));
  }
};

struct BananutBlack : app::SvgPort {
  BananutBlack() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/BananutBlack.svg")));
  }
};

struct BefacoToggle : app::SvgSwitch {
  BefacoToggle() {
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/KI1H-Switch_0.svg")));
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/KI1H-Switch_2.svg")));
  }
};

// Custom ice-blue knob (drop-in replacement for RoundBlackKnob). Same 28.35px
// footprint, so existing createParamCentered<> layout math is unchanged.
struct KI1HKnob : RoundKnob {
  KI1HKnob() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-RoundBlackKnob.svg")));
    bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-RoundBlackKnob_bg.svg")));
  }
};

// Custom ice-blue big knob (drop-in replacement for RoundBigBlackKnob, 45px).
struct KI1HBigKnob : RoundKnob {
  KI1HBigKnob() {
    setSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-RoundBigBlackKnob.svg")));
    bg->setSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-RoundBigBlackKnob_bg.svg")));
  }
};

// Custom ice-blue slider (drop-in replacement for BefacoSlidePot). Track and
// handle share the stock dimensions, so the handle-travel geometry matches.
struct KI1HSlidePot : app::SvgSlider {
  KI1HSlidePot() {
    setBackgroundSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-SlidePot.svg")));
    setHandleSvg(Svg::load(asset::plugin(pluginInstance, "res/KI1H-SlidePotHandle.svg")));
    math::Vec margin = math::Vec(3.5, 3.5);
    // Handle is 17.25px wide (wider than the 8.59px track), so recentre X on the
    // track: track centre X = 3.5 + 8.59132/2 = 7.796, handle left = 7.796 - 17.25/2.
    setHandlePos(math::Vec(-4.329, 87).plus(margin), math::Vec(-4.329, -2).plus(margin));
    background->box.pos = margin;
    box.size = background->box.size.plus(margin.mult(2));
  }
};

// Custom ice-blue 3-position switch (drop-in replacement for BefacoSwitch).
struct KI1HSwitch : app::SvgSwitch {
  KI1HSwitch() {
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/KI1H-Switch_0.svg")));
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/KI1H-Switch_1.svg")));
    addFrame(Svg::load(asset::plugin(pluginInstance, "res/KI1H-Switch_2.svg")));
  }
};
