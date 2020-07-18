/*
 * test_caffe_plugin.c
 * 
 * Copyright 2020 Che Hongwei <htc.chehw@gmail.com>
 * 
 * The MIT License (MIT)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "input-frame.h"
#include "ann-plugin.h"
#include "ai-engine.h"

#include <cairo/cairo.h>
#include <json-c/json.h>
#include "utils.h"

static const char * s_model_cfg = "models/ResNet-101-deploy.prototxt";
static const char * s_weights_file = "models/ResNet-101-model.caffemodel";
static const char * s_mean_file = "models/ResNet_mean.binaryproto";

int main(int argc, char **argv)
{
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	ai_engine_t * engine = ai_engine_init(NULL, "ai-engine::caffe", NULL);
	assert(engine);
	
	json_object * jconfig = json_object_new_object();
	json_object_object_add(jconfig, "conf_file", json_object_new_string(s_model_cfg));
	json_object_object_add(jconfig, "weights_file", json_object_new_string(s_weights_file));
	json_object_object_add(jconfig, "means_file", json_object_new_string(s_mean_file));
	
	int rc = engine->init(engine, jconfig);
	assert(0 == rc);
	
	input_frame_t * frame = input_frame_new();
	frame->type = input_frame_type_bgra;
	
	unsigned char * png_data = NULL;
	ssize_t cb_data = load_binary_data("1.png", &png_data);
	
	input_frame_set_png(frame, png_data, cb_data, NULL, 0);
	


	json_object * jresults = NULL;
	rc = engine->predict(engine, frame, &jresults);
	assert(0 == rc && jresults);
	
	
	printf("results: %s\n", json_object_to_json_string_ext(jresults, JSON_C_TO_STRING_PRETTY));
	json_object_put(jresults);
	
	ai_engine_cleanup(engine);
	
	return 0;
}

