#include "plugin.hpp"


struct RMod : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT_INPUT,
		INPUT_CARRIER,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	RMod() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	}

    float tanhApprox(float amplitude) {
        return amplitude / (1 + abs(amplitude)) * 5;
    }
    
	void process(const ProcessArgs& args) override {
        float input = inputs[INPUT_INPUT].getVoltage();
        float carrier = inputs[INPUT_CARRIER].getVoltage();
        outputs[OUTPUT_OUTPUT].setVoltage(tanhApprox(input * carrier));
    }
};


struct RModWidget : ModuleWidget {
	RModWidget(RMod* module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/RMod.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(7.292, 112.182)), module, RMod::INPUT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(20.349, 112.192)), module, RMod::INPUT_CARRIER));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(33.308, 112.182)), module, RMod::OUTPUT_OUTPUT));
	}
};


Model* modelRMod = createModel<RMod, RModWidget>("RMod");
