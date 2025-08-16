#include "plugin.hpp"

Plugin *pluginInstance;

void init(Plugin *p) {
  pluginInstance = p;

  // Add modules here
  p->addModel(modelKI1H_VCO);
  p->addModel(modelKI1H_LFO);
  p->addModel(modelKI1H_MIX);
  p->addModel(modelKI1H_FILTER);
  // Any other plugin initialization may go here.
  // As an alternative, consider lazy-loading assets and lookup tables when your module is created
  // to reduce startup times of Rack.
}
