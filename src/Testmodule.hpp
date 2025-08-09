#pragma once
#include "plugin.hpp"

struct Testmodule : Module {
	enum ParamIds {
		PITCH_PARAM,
		WAVE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		WAVE_OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BLINK_LIGHT,
		NUM_LIGHTS
	};
	enum Waves {
		WAVE_TRI,
		WAVE_SAW,
		WAVE_SQ,
		WAVE_PWM
	};

	Testmodule();
	void process(const ProcessArgs &args) override;

private:
	float phase = 0.f;
	float blinkPhase = 0.f;
};

struct TestmoduleWidget : ModuleWidget {
	TestmoduleWidget(Testmodule* module);
}; 
