#include "plugin.hpp"

// We want to make two AD and two ASR envelopes. When the AD out is not connected,
// The envelope section should behave as an AHDSR env, otherwise it should act as
// an AD env and an AR/ASR env with swichable behaviour

// ============================================================================
// CLASS DEFINITION
// ============================================================================
struct Envelope {

  enum Stage { STAGE_OFF, STAGE_ATTACK, STAGE_SUSTAIN, STAGE_RELEASE };
  float env = 0.f;
  float eoa = 0.f;
  float eor = 1.f;

  // End-of-attack is a 1 ms trigger pulse, not a latched level: processTransition
  // fires it at the instant attack completes, and evolveEnvelope derives `eoa`
  // from it every frame. A pulse cannot stick high through a held sustain the way
  // a latched level could, which is what pinned the ASD's EOA jack at 10 V.
  dsp::PulseGenerator eoaPulse;

  Stage stage = STAGE_OFF;
  float envState = 0.f;
  float attackTime = 0.1f, releaseTime = 0.1f;

  void retrigger() {
    eoa = 0.f;
    eor = 1.f;
    eoaPulse.reset();
    stage = STAGE_ATTACK;
    env = envState = 0.f;
  }

  /** Restores exactly the state a freshly constructed envelope has. */
  void reset() {
    env = 0.f;
    eoa = 0.f;
    eor = 1.f;
    eoaPulse.reset();
    stage = STAGE_OFF;
    envState = 0.f;
  }

  /** Advances envState for the current stage. Shared by both subclasses; the
  only stage that behaves differently between them is the transition logic,
  which is processTransition's job. */
  void evolveEnvelope(const float &sampleTime) {
    switch (stage) {
    case STAGE_ATTACK: {
      envState += sampleTime / attackTime;
      env = std::min(envState, 1.f);
      break;
    }
    case STAGE_RELEASE: {
      envState -= sampleTime / releaseTime;
      env = std::max(0.f, envState);
      break;
    }
    case STAGE_SUSTAIN: {
      // Held at its current level; only ASDEnvelope ever reaches this.
      break;
    }
    case STAGE_OFF: {
      env = 0.0f;
      break;
    }
    }
    // Advance the end-of-attack trigger and expose its level. eoaPulse.trigger()
    // is called from processTransition (which runs just before this each frame).
    eoa = eoaPulse.process(sampleTime) ? 1.f : 0.f;
  }
};

/** Attack then straight into release. Never reaches STAGE_SUSTAIN. */
struct ADEnvelope : Envelope {

  // AD ignores the gate after it starts: attack runs to completion, then release.
  void processTransition() {
    if (stage == STAGE_ATTACK) {
      if (envState >= 1.0f) {
        eoaPulse.trigger(1e-3f);
        eor = 0.f;
        env = envState = 1.0f;
        stage = STAGE_RELEASE;
      }
    } else if (stage == STAGE_RELEASE) {
      if (envState <= 0.f) {
        eor = 1.f;
        stage = STAGE_OFF;
        env = envState = 0.f;
      }
    }
  }

  void process(const float &sampleTime) {
    processTransition();
    evolveEnvelope(sampleTime);
  }
};

/** Attack to a sustain level, hold there while gated (when asr is set), then
release. The sustain stage is the only behavioural difference from AD. */
struct ASDEnvelope : Envelope {

  float sustain = 1.f;

  void processTransition(const bool asr, const bool held) {
    if (stage == STAGE_ATTACK) {
      eor = 0.f;
      if (envState >= sustain) {
        eoaPulse.trigger(1e-3f);
        env = envState = sustain;
        if (asr) {
          stage = STAGE_SUSTAIN;
        } else {
          stage = STAGE_RELEASE;
        }
      }
    } else if (stage == STAGE_SUSTAIN) {
      if (!held) {
        stage = STAGE_RELEASE;
      }
    } else if (stage == STAGE_RELEASE) {
      if (envState <= 0.f) {
        eor = 1.f;
        stage = STAGE_OFF;
        env = envState = 0.f;
      }
    }
  }

  void process(const float &sampleTime, const bool sus, const bool held) {
    processTransition(sus, held);
    evolveEnvelope(sampleTime);
  }
};

// ============================================================================
// MODULE DEFINITION
// ============================================================================
struct KI1H_ENVELOPE : Module {
  enum ParamIds {
    ATK1_PARAM,
    ATK2_PARAM,
    ATK3_PARAM,
    ATK4_PARAM,
    REL3_PARAM,
    REL4_PARAM,
    SUS2_PARAM,
    SUS_PARAM,
    REL1_PARAM,
    REL2_PARAM,
    ASR1_PARAM,
    ASR2_PARAM,
    NUM_PARAMS
  };
  enum InputIds { TRIGGER1_INPUT, TRIGGER2_INPUT, TRIGGER3_INPUT, TRIGGER4_INPUT, NUM_INPUTS };
  enum OutputIds {
    OUT1_OUTPUT,
    OUT2_OUTPUT,
    OUT3_OUTPUT,
    OUT4_OUTPUT,
    EOA1_OUTPUT,
    EOA2_OUTPUT,
    EOA3_OUTPUT,
    EOA4_OUTPUT,
    EOR1_OUTPUT,
    EOR2_OUTPUT,
    EOR3_OUTPUT,
    EOR4_OUTPUT,
    NUM_OUTPUTS
  };

  // [0]=AD1 [1]=ASD1 [2]=AD2 [3]=ASD2
  dsp::SchmittTrigger gateTrigger[4];

  // Frames to latch, but not act on, trigger edges after a load or reset. Every
  // output port starts at 0 V, so an EOR that should already be resting high
  // produces a spurious 0->10 V rising edge one sample later. With an EOR jack
  // patched back to a trigger input (a self-cycling envelope) that false edge
  // fires every envelope in the ring at once, so a chain that was ping-ponging
  // in series when the patch was saved comes back running in parallel — roughly
  // twice as fast. During these frames the SchmittTriggers still latch, so they
  // absorb the startup edge without firing, and the restored phase survives.
  int loadSettleFrames = 0;
  static constexpr int kLoadSettleFrames = 2;

  KI1H_ENVELOPE();
  void process(const ProcessArgs &args) override;

  // Rack persists only params, so without these the envelopes restart cold on
  // every load and a chained pair loses the phase relationship it was saved at.
  // Persist each envelope's live stage/level so a reloaded patch resumes where
  // it left off; see loadSettleFrames for why that alone is not enough.
  json_t *dataToJson() override;
  void dataFromJson(json_t *root) override;

  void onReset(const ResetEvent &e) override {
    Module::onReset(e);
    for (int i = 0; i < 2; i++) {
      ad[i].reset();
      asd[i].reset();
    }
    loadSettleFrames = kLoadSettleFrames;
  }

  static constexpr float minStageTime = 0.003f; // in seconds
  static constexpr float maxStageTime = 10.f;   // in seconds

  static float convertCVToTimeInSeconds(float cv) {
    return minStageTime * std::pow(maxStageTime / minStageTime, cv);
  }

private:
  ADEnvelope ad[2];
  ASDEnvelope asd[2];
  static constexpr float CV_SCALE = 10.f;
};

// ============================================================================
// WIDGET DEFINITION
// ============================================================================
struct KI1H_ENVELOPEWidget : ModuleWidget {
  KI1H_ENVELOPEWidget(KI1H_ENVELOPE *module);
};

// ============================================================================
// MODULE CONFIGURATION
// ============================================================================
KI1H_ENVELOPE::KI1H_ENVELOPE() {
  config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS);
  configParam(ATK1_PARAM, 0.f, 1.f, 0.1f, "AD1 Attack");
  configParam(ATK2_PARAM, 0.f, 1.f, 0.1f, "ASD1 Attack");
  configParam(ATK3_PARAM, 0.f, 1.f, 0.1f, "AD2 Attack");
  configParam(ATK4_PARAM, 0.f, 1.f, 0.1f, "ASD2 Attack");
  configParam(REL1_PARAM, 0.f, 1.f, 0.1f, "AD1 Release");
  configParam(REL2_PARAM, 0.f, 1.f, 0.1f, "ASD1 Release");
  configParam(REL3_PARAM, 0.f, 1.f, 0.1f, "AD2 Release");
  configParam(REL4_PARAM, 0.f, 1.f, 0.1f, "ASD2 Release");
  configParam(SUS_PARAM, 0.f, 1.f, 0.1f, "Sustain");
  configParam(SUS2_PARAM, 0.f, 1.f, 0.1f, "Sustain2");
  configParam(ASR1_PARAM, 0.f, 1.f, 0.f, "AR/ASR Switch1");
  configParam(ASR2_PARAM, 0.f, 1.f, 0.f, "AR/ASR Switch2");
  configInput(TRIGGER1_INPUT, "AD1 Trigger");
  configInput(TRIGGER2_INPUT, "ASD1 Trigger");
  configInput(TRIGGER3_INPUT, "AD2 Trigger");
  configInput(TRIGGER4_INPUT, "ASD2 Trigger");

  configOutput(EOA1_OUTPUT, "AD1 End of Attack");
  configOutput(EOA2_OUTPUT, "ASD1 End of Attack");
  configOutput(EOA3_OUTPUT, "AD2 End of Attack");
  configOutput(EOA4_OUTPUT, "ASD2 End of Attack");
  configOutput(EOR1_OUTPUT, "AD1 End of Release");
  configOutput(EOR2_OUTPUT, "ASD1 End of Release");
  configOutput(EOR3_OUTPUT, "AD2 End of Release");
  configOutput(EOR4_OUTPUT, "ASD2 End of Release");
  configOutput(OUT1_OUTPUT, "AD1 Output");
  configOutput(OUT2_OUTPUT, "ASD1 Output");
  configOutput(OUT3_OUTPUT, "AD2 Output");
  configOutput(OUT4_OUTPUT, "ASD2 Output");

  // A freshly placed module also starts with every output at 0 V, so the same
  // startup edge would auto-start a self-patched ring in parallel. Settle it.
  loadSettleFrames = kLoadSettleFrames;
}

void KI1H_ENVELOPE::process(const ProcessArgs &args) {
  // ATK, TRIGGER, OUT, EOA and EOR all stride by 2 between the two AD/ASD
  // pairs, so those index arithmetically off the first member of each enum.
  //
  // The release and sustain params do not: they were declared out of order
  // (REL3, REL4, SUS2, SUS, REL1, REL2). Renumbering them is not an option —
  // Rack serializes params by index, so it would silently break every saved
  // patch — so look those two up instead.
  static const int adRelParam[2] = {REL1_PARAM, REL3_PARAM};
  static const int asdRelParam[2] = {REL2_PARAM, REL4_PARAM};
  static const int asdSusParam[2] = {SUS_PARAM, SUS2_PARAM};

  // See loadSettleFrames: swallow trigger edges (but keep latching them) for the
  // first frames after a load/reset so the 0 V -> resting-level jump on the jacks
  // does not fire a self-patched ring.
  const bool settling = loadSettleFrames > 0;
  if (loadSettleFrames > 0)
    loadSettleFrames--;

  for (int i = 0; i < 2; i++) {
    const int adIdx = 2 * i;      // AD1, then AD2
    const int asdIdx = 2 * i + 1; // ASD1, then ASD2

    // Each AD/ASD pair is self-contained: within a pair the AD's end-of-attack
    // normals into the ASD's trigger, but nothing crosses between the pairs. So
    // a pair whose six outputs are all empty can be skipped whole, which also
    // skips its four convertCVToTimeInSeconds calls (a std::pow each).
    const bool pairLive =
        outputs[OUT1_OUTPUT + adIdx].isConnected() || outputs[OUT1_OUTPUT + asdIdx].isConnected() ||
        outputs[EOA1_OUTPUT + adIdx].isConnected() || outputs[EOA1_OUTPUT + asdIdx].isConnected() ||
        outputs[EOR1_OUTPUT + adIdx].isConnected() || outputs[EOR1_OUTPUT + asdIdx].isConnected();
    if (!pairLive)
      continue;

    // ========================================================================
    // AD STAGE
    // ========================================================================
    ad[i].attackTime = convertCVToTimeInSeconds(params[ATK1_PARAM + adIdx].getValue());
    ad[i].releaseTime = convertCVToTimeInSeconds(params[adRelParam[i]].getValue());

    const bool adTriggered =
        gateTrigger[adIdx].process(inputs[TRIGGER1_INPUT + adIdx].getVoltage());
    if (adTriggered && !settling)
      ad[i].retrigger();

    ad[i].process(args.sampleTime);

    outputs[OUT1_OUTPUT + adIdx].setVoltage(ad[i].env * CV_SCALE);
    outputs[EOA1_OUTPUT + adIdx].setVoltage(ad[i].eoa * CV_SCALE);
    outputs[EOR1_OUTPUT + adIdx].setVoltage(ad[i].eor * CV_SCALE);

    // ========================================================================
    // ASD STAGE
    // ========================================================================
    asd[i].attackTime = convertCVToTimeInSeconds(params[ATK1_PARAM + asdIdx].getValue());
    asd[i].sustain = params[asdSusParam[i]].getValue();
    asd[i].releaseTime = convertCVToTimeInSeconds(params[asdRelParam[i]].getValue());

    // With nothing patched into the ASD's own trigger, the pair acts as one
    // AHDSR: the ASD is fired by the AD's end-of-attack.
    const bool chained = !inputs[TRIGGER1_INPUT + asdIdx].isConnected();
    const float asdTrigPulse = chained ? ad[i].eoa * CV_SCALE
                                       : inputs[TRIGGER1_INPUT + asdIdx].getVoltage();

    const bool asdTriggered = gateTrigger[asdIdx].process(asdTrigPulse);

    // When chained, sustain has to be held by the AD stage's real gate. The
    // signal driving gateTrigger[asdIdx] is end-of-attack, which is a pulse,
    // not a gate, so holding off it would barely sustain at all.
    const bool asdHeld = chained ? gateTrigger[adIdx].isHigh() : gateTrigger[asdIdx].isHigh();
    const bool asr = params[ASR1_PARAM + i].getValue() > 0.f;
    if (asdTriggered && !settling)
      asd[i].retrigger();

    asd[i].process(args.sampleTime, asr, asdHeld);

    // In chained mode the pair's output is the louder of the two stages, so
    // the AD's attack is not swallowed by the ASD still being at zero.
    float asdVolt = asd[i].env;
    if (chained && ad[i].env > asdVolt)
      asdVolt = ad[i].env;

    outputs[OUT1_OUTPUT + asdIdx].setVoltage(asdVolt * CV_SCALE);
    outputs[EOA1_OUTPUT + asdIdx].setVoltage(asd[i].eoa * CV_SCALE);
    outputs[EOR1_OUTPUT + asdIdx].setVoltage(asd[i].eor * CV_SCALE);
  }
}

// ============================================================================
// STATE PERSISTENCE
// ============================================================================
// The four envelopes in index order: [0]=AD1 [1]=ASD1 [2]=AD2 [3]=ASD2. sustain
// (ASD only) is omitted — it is re-derived from its param every sample. The
// SchmittTrigger gate states are omitted too: they re-latch from the jacks
// during the loadSettleFrames window, so they need no persisting.
json_t *KI1H_ENVELOPE::dataToJson() {
  Envelope *env[4] = {&ad[0], &asd[0], &ad[1], &asd[1]};
  json_t *root = json_object();
  json_t *envs = json_array();
  for (int i = 0; i < 4; i++) {
    json_t *e = json_object();
    json_object_set_new(e, "stage", json_integer(env[i]->stage));
    json_object_set_new(e, "env", json_real(env[i]->env));
    json_object_set_new(e, "envState", json_real(env[i]->envState));
    json_object_set_new(e, "eoa", json_real(env[i]->eoa));
    json_object_set_new(e, "eor", json_real(env[i]->eor));
    json_array_append_new(envs, e);
  }
  json_object_set_new(root, "envelopes", envs);
  return root;
}

void KI1H_ENVELOPE::dataFromJson(json_t *root) {
  json_t *envs = json_object_get(root, "envelopes");
  if (!envs)
    return;
  Envelope *env[4] = {&ad[0], &asd[0], &ad[1], &asd[1]};
  for (int i = 0; i < 4; i++) {
    json_t *e = json_array_get(envs, i);
    if (!e)
      continue;
    if (json_t *j = json_object_get(e, "stage"))
      env[i]->stage = (Envelope::Stage)json_integer_value(j);
    if (json_t *j = json_object_get(e, "env"))
      env[i]->env = json_number_value(j);
    if (json_t *j = json_object_get(e, "envState"))
      env[i]->envState = json_number_value(j);
    if (json_t *j = json_object_get(e, "eoa"))
      env[i]->eoa = json_number_value(j);
    if (json_t *j = json_object_get(e, "eor"))
      env[i]->eor = json_number_value(j);
  }
  // Restored a running state: swallow the load-time startup edge so the ring
  // resumes at its saved phase instead of re-syncing.
  loadSettleFrames = kLoadSettleFrames;
}

KI1H_ENVELOPEWidget::KI1H_ENVELOPEWidget(KI1H_ENVELOPE *module) {
  setModule(module);
  setPanel(createPanel(asset::plugin(pluginInstance, "res/KI1H-ENVELOPE.svg")));

  // ============================================================================
  // PANEL SCREWS
  // ============================================================================
  addPanelScrews(this);
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[0], ROWS[1])), module,
                                               KI1H_ENVELOPE::ATK1_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[1], ROWS[1])), module,
                                               KI1H_ENVELOPE::REL1_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[2], ROWS[1])), module,
                                               KI1H_ENVELOPE::ATK2_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[3], ROWS[1])), module,
                                               KI1H_ENVELOPE::SUS_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[4], ROWS[1])), module,
                                               KI1H_ENVELOPE::REL2_PARAM));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::TRIGGER1_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA1_OUTPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR1_OUTPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[1], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::OUT1_OUTPUT));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[2], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::TRIGGER2_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOA2_OUTPUT));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[3], ROWS[2] + HALF_R / 2)), module,
                                             KI1H_ENVELOPE::ASR1_PARAM));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[0])), module,
                                             KI1H_ENVELOPE::EOR2_OUTPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[4], ROWS[2] + HALF_R / 2)), module,
                                              KI1H_ENVELOPE::OUT2_OUTPUT));

  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[0], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::ATK3_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[1], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::REL3_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[2], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::ATK4_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[3], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::SUS2_PARAM));
  addParam(createParamCentered<KI1HSlidePot>(mm2px(Vec(COLUMNS[4], ROWS[4] - HALF_R / 2)), module,
                                               KI1H_ENVELOPE::REL4_PARAM));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[0], ROWS[5])), module,
                                              KI1H_ENVELOPE::TRIGGER3_INPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[0] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA3_OUTPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[1] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR3_OUTPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[1], ROWS[5])), module,
                                              KI1H_ENVELOPE::OUT3_OUTPUT));
  addInput(createInputCentered<BananutOrange>(mm2px(Vec(COLUMNS[2], ROWS[5])), module,
                                              KI1H_ENVELOPE::TRIGGER4_INPUT));
  addParam(createParamCentered<BefacoToggle>(mm2px(Vec(COLUMNS[3], ROWS[5])), module,
                                             KI1H_ENVELOPE::ASR2_PARAM));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[2] + HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOA4_OUTPUT));
  addOutput(createOutputCentered<BananutRed>(mm2px(Vec(COLUMNS[4] - HALF_C, ROWS[3] - HALF_R / 2)),
                                             module, KI1H_ENVELOPE::EOR4_OUTPUT));
  addOutput(createOutputCentered<BananutBlue>(mm2px(Vec(COLUMNS[4], ROWS[5])), module,
                                              KI1H_ENVELOPE::OUT4_OUTPUT));
}

Model *modelKI1H_ENVELOPE = createModel<KI1H_ENVELOPE, KI1H_ENVELOPEWidget>("KI1H-ENVELOPE");
