#include "plugin.hpp"


using namespace simd;


const float MIN_TIME = 1e-3f;
const float MAX_TIME = 10.f;
const float LAMBDA_BASE = MAX_TIME / MIN_TIME;


struct HADSR : Module {
    enum ParamIds {
        HOLD_PARAM,
        ATTACK_PARAM,
        DECAY_PARAM,
        SUSTAIN_PARAM,
        RELEASE_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        DECAY_INPUT,
        SUSTAIN_INPUT,
        RELEASE_INPUT,
        GATE_INPUT,
        TRIG_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        ENVELOPE_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        ATTACK_LIGHT,
        DECAY_LIGHT,
        SUSTAIN_LIGHT,
        RELEASE_LIGHT,
        NUM_LIGHTS
    };

    float_4 attacking[4] = {float_4::zero()};
    float_4 env[4] = {0.f};
    dsp::TSchmittTrigger<float_4> trigger[4];
    dsp::ClockDivider cvDivider;
    float_4 attackLambda[4] = {0.f};
    float_4 decayLambda[4] = {0.f};
    float_4 releaseLambda[4] = {0.f};
    float_4 sustain[4] = {0.f};
    dsp::ClockDivider lightDivider;
    float_4 hold = 0.f;

    HADSR() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(ATTACK_PARAM, 0.f, 1.f, 0.5f, "Attack", " ms", LAMBDA_BASE, MIN_TIME * 1000);
        configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " ms", LAMBDA_BASE, MIN_TIME * 1000);
        configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.5f, "Sustain", "%", 0, 100);
        configParam(RELEASE_PARAM, 0.f, 1.f, 0.5f, "Release", " ms", LAMBDA_BASE, MIN_TIME * 1000);
        configParam(HOLD_PARAM, 0.f, 1.f, 0.0f, "Hold", " ms");
//        configInput(DECAY_INPUT, "Decay");
//        configInput(SUSTAIN_INPUT, "Sustain");
//        configInput(RELEASE_INPUT, "Release");
//        configInput(GATE_INPUT, "Gate");
//        configInput(TRIG_INPUT, "Retrigger");
//        configOutput(ENVELOPE_OUTPUT, "Envelope");

        cvDivider.setDivision(16);
        lightDivider.setDivision(128);
    }

    void process(const ProcessArgs& args) override {
        // 0.16-0.19 us serial
        // 0.23 us serial with all lambdas computed
        // 0.15-0.18 us serial with all lambdas computed with SSE

        int channels = inputs[GATE_INPUT].getChannels();

        // Compute lambdas
        if (cvDivider.process()) {
            float attackParam = params[ATTACK_PARAM].getValue();
            float decayParam = params[DECAY_PARAM].getValue();
            float sustainParam = params[SUSTAIN_PARAM].getValue();
            float releaseParam = params[RELEASE_PARAM].getValue();

            for (int c = 0; c < channels; c += 4) {
                // CV
                float_4 attack = attackParam; //+ inputs[ATTACK_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f;
                float_4 decay = decayParam + inputs[DECAY_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f;
                float_4 sustain = sustainParam + inputs[SUSTAIN_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f;
                float_4 release = releaseParam + inputs[RELEASE_INPUT].getPolyVoltageSimd<float_4>(c) / 10.f;

                attack = simd::clamp(attack, 0.f, 1.f);
                decay = simd::clamp(decay, 0.f, 1.f);
                sustain = simd::clamp(sustain, 0.f, 1.f);
                release = simd::clamp(release, 0.f, 1.f);

                attackLambda[c / 4] = simd::pow(LAMBDA_BASE, -attack) / MIN_TIME;
                decayLambda[c / 4] = simd::pow(LAMBDA_BASE, -decay) / MIN_TIME;
                releaseLambda[c / 4] = simd::pow(LAMBDA_BASE, -release) / MIN_TIME;
                this->sustain[c / 4] = sustain;
            }
        }

        float_4 gate[4];
        float_4 preGate[4];
        float_4 holdEnd;
        for (int c = 0; c < channels; c += 4) {
            // Gate
            // todo: if is on hold -> gate is all zeroes
            // otherwiee -> get from input

            preGate[c / 4] = inputs[GATE_INPUT].getVoltageSimd<float_4>(c) >= 1.f;
            
            hold = simd::ifelse(preGate[c / 4], hold + args.sampleTime, 0.f);
            holdEnd = hold >= params[HOLD_PARAM].getValue();
            gate[c / 4] = simd::ifelse(holdEnd, preGate[c / 4], 0.f);
                   
            // Retrigger
            float_4 preTriggered = trigger[c / 4].process(inputs[TRIG_INPUT].getPolyVoltageSimd<float_4>(c));
            hold = simd::ifelse(preTriggered, 0.f, hold);

            holdEnd = hold >= params[HOLD_PARAM].getValue();
            float_4 triggered = simd::ifelse(holdEnd, preTriggered, 0.f);
            attacking[c / 4] = simd::ifelse(triggered, float_4::mask(), attacking[c / 4]);

            // Get target and lambda for exponential decay
            const float attackTarget = 1.2f;
            float_4 target = simd::ifelse(gate[c / 4], simd::ifelse(attacking[c / 4], attackTarget, sustain[c / 4]), 0.f);
            float_4 lambda = simd::ifelse(gate[c / 4], simd::ifelse(attacking[c / 4], attackLambda[c / 4], decayLambda[c / 4]), releaseLambda[c / 4]);

            // Adjust env
            env[c / 4] += (target - env[c / 4]) * lambda * args.sampleTime;

            // Turn off attacking state if envelope is HIGH
            attacking[c / 4] = simd::ifelse(env[c / 4] >= 1.f, float_4::zero(), attacking[c / 4]);

            // Turn on attacking state if gate is LOW
            attacking[c / 4] = simd::ifelse(gate[c / 4], attacking[c / 4], float_4::mask());

            // Set output
            outputs[ENVELOPE_OUTPUT].setVoltageSimd(10.f * env[c / 4], c);
        }

        outputs[ENVELOPE_OUTPUT].setChannels(channels);

        // Lights
        if (lightDivider.process()) {
            lights[ATTACK_LIGHT].setBrightness(0);
            lights[DECAY_LIGHT].setBrightness(0);
            lights[SUSTAIN_LIGHT].setBrightness(0);
            lights[RELEASE_LIGHT].setBrightness(0);

            for (int c = 0; c < channels; c += 4) {
                const float epsilon = 0.01f;
                float_4 sustaining = (sustain[c / 4] <= env[c / 4]) & (env[c / 4] < sustain[c / 4] + epsilon);
                float_4 resting = (env[c / 4] < epsilon);

                if (simd::movemask(gate[c / 4] & attacking[c / 4]))
                    lights[ATTACK_LIGHT].setBrightness(1);
                if (simd::movemask(gate[c / 4] & ~attacking[c / 4] & ~sustaining))
                    lights[DECAY_LIGHT].setBrightness(1);
                if (simd::movemask(gate[c / 4] & ~attacking[c / 4] & sustaining))
                    lights[SUSTAIN_LIGHT].setBrightness(1);
                if (simd::movemask(~gate[c / 4] & ~resting))
                    lights[RELEASE_LIGHT].setBrightness(1);
            }
        }
    }
};


struct HADSRWidget : ModuleWidget {
    HADSRWidget(HADSR* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/HADSR.svg")));

        addChild(createWidget<ScrewSilver>(Vec(15, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
        addChild(createWidget<ScrewSilver>(Vec(15, 365)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        addParam(createParam<RoundLargeBlackKnob>(Vec(62, 57), module, HADSR::ATTACK_PARAM));
        addParam(createParam<RoundLargeBlackKnob>(Vec(62, 124), module, HADSR::DECAY_PARAM));
        addParam(createParam<RoundLargeBlackKnob>(Vec(62, 191), module, HADSR::SUSTAIN_PARAM));
        addParam(createParam<RoundLargeBlackKnob>(Vec(62, 257), module, HADSR::RELEASE_PARAM));

        addParam(createParam<RoundSmallBlackKnob>(Vec(9, 63), module, HADSR::HOLD_PARAM));
        addInput(createInput<PJ301MPort>(Vec(9, 129), module, HADSR::DECAY_INPUT));
        addInput(createInput<PJ301MPort>(Vec(9, 196), module, HADSR::SUSTAIN_INPUT));
        addInput(createInput<PJ301MPort>(Vec(9, 263), module, HADSR::RELEASE_INPUT));

        addInput(createInput<PJ301MPort>(Vec(9, 320), module, HADSR::GATE_INPUT));
        addInput(createInput<PJ301MPort>(Vec(48, 320), module, HADSR::TRIG_INPUT));
        addOutput(createOutput<PJ301MPort>(Vec(87, 320), module, HADSR::ENVELOPE_OUTPUT));

        addChild(createLight<SmallLight<RedLight>>(Vec(94, 41), module, HADSR::ATTACK_LIGHT));
        addChild(createLight<SmallLight<RedLight>>(Vec(94, 109), module, HADSR::DECAY_LIGHT));
        addChild(createLight<SmallLight<RedLight>>(Vec(94, 175), module, HADSR::SUSTAIN_LIGHT));
        addChild(createLight<SmallLight<RedLight>>(Vec(94, 242), module, HADSR::RELEASE_LIGHT));
    }
};


Model* modelHADSR = createModel<HADSR, HADSRWidget>("HADSR");
