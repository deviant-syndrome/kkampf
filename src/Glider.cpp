#include "plugin.hpp"
#include <cmath>

struct Glider : Module {
    enum ParamIds {
        SPEED_PARAM,
        VELSENV_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        INPUT_INPUT,
        VELCV_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        OUTPUT_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    float currentVoltage;

	Glider() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SPEED_PARAM, 0.0005f, 0.002f, 0.001f, "");
        configParam(VELSENV_PARAM, 0.0f, 0.1f, 0.0f, "");
        currentVoltage = 0;
    }

	void process(const ProcessArgs& args) override {
        float delta = inputs[INPUT_INPUT].getVoltage() - currentVoltage;
        float velBoost = 1;
        
        velBoost = velBoost + inputs[VELCV_INPUT].getVoltage() * params[VELSENV_PARAM].getValue();
        
        if (abs(delta) > 0.0001f) {
            currentVoltage = currentVoltage + delta * params[SPEED_PARAM].getValue() * velBoost;//0.001f;
        }
        outputs[OUTPUT_OUTPUT].setVoltage(currentVoltage);
	}
};


struct GliderWidget : ModuleWidget {
	GliderWidget(Glider* module) {
        setModule(module);
        setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/Glider.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.7, 31.465)), module, Glider::SPEED_PARAM));
        addParam(createParamCentered<RoundBlackKnob>(mm2px(Vec(12.7, 52.73)), module, Glider::VELSENV_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.214, 112.182)), module, Glider::INPUT_INPUT));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.7, 74.655)), module, Glider::VELCV_INPUT));

        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(18.214, 112.182)), module, Glider::OUTPUT_OUTPUT));
	}
};


Model* modelGlider = createModel<Glider, GliderWidget>("Glider");
