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

// Programmatically generated row positions using constexpr function
constexpr float getRowPosition(int row) {
  return ROW_START + (row - 1) * ROW_SPACING;
}

// Alternative: Access via array (0-indexed, so ROWS[0] = ROW1)
constexpr std::array<float, NUM_ROWS> ROWS = {getRowPosition(1), getRowPosition(2),
                                              getRowPosition(3), getRowPosition(4),
                                              getRowPosition(5), getRowPosition(6)};
// Programmatically generated row positions using constexpr function
constexpr float getColumnPosition(int column) {
  return COLUMN_START + (column - 1) * COLUMN_SPACING;
}

// Alternative: Access via array (0-indexed, so ROWS[0] = ROW1)
constexpr std::array<float, NUM_COLUMNS> COLUMNS = {getColumnPosition(1), getColumnPosition(2),
                                                    getColumnPosition(3), getColumnPosition(4),
                                                    getColumnPosition(5)};
