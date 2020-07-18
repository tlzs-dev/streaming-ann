/*
 * caffe-model.c
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

#include <json-c/json.h>
#include "utils.h"
#include "caffe-model.h"
#include "ai-engine.h"
#include "input-frame.h"

#define AI_PLUGIN_TYPE_STRING "ai-engine::caffe"

/************************************
 * caffe_tensor
 ***********************************/
caffe_tensor_t * caffe_tensor_init(caffe_tensor_t * tensor, const char *name, const int dims[static 4],  float * data)
{
	if(NULL == tensor) tensor = calloc(1, sizeof(*tensor));
	assert(tensor);
	
	size_t size = 0;
	if(name) tensor->name = strdup(name);
	if(dims) {
		size = dims[0] * dims[1] * dims[2] * dims[3];
		memcpy(tensor->dims, dims, sizeof(tensor->dims));
	}
	if(data) {
		assert(size > 0);
		tensor->data = malloc(size * sizeof(*data));
		assert(tensor->data);
		memcpy(tensor->data, data, size * sizeof(*data));
	} 
	return tensor;
}
void caffe_tensor_cleanup(caffe_tensor_t * tensor)
{
	if(NULL == tensor) return;
	if(tensor->name) { free(tensor->name); tensor->name = NULL; };
	if(tensor->data) { free(tensor->data); tensor->data = NULL; };
	return;
}

/************************************
 * caffe-model
 ***********************************/
/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {	// to make sure there are no name manglings 
#endif
const char * ann_plugin_get_type(void);
static void ai_plugin_caffe_cleanup(struct ai_engine * engine);
static int ai_plugin_caffe_load_config(struct ai_engine * engine, json_object * jconfig);
static int ai_plugin_caffe_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults);

int ann_plugin_init(ai_engine_t * engine, json_object * jconfig);

#ifdef __cplusplus
}
#endif


const char * ann_plugin_get_type(void)
{
	return AI_PLUGIN_TYPE_STRING;
}

static void ai_plugin_caffe_cleanup(struct ai_engine * engine)
{
	return;
}

static int ai_plugin_caffe_load_config(struct ai_engine * engine, json_object * jconfig)
{
	return 0;
}

static int ai_plugin_caffe_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	debug_printf("%s(): frame: type=%d, size=%d x %d", __FUNCTION__,
		frame->type,
		frame->width, frame->height);
		
	int rc = -1;
	caffe_model_plugin_t * plugin = engine->priv;
	assert(plugin);

	bgra_image_t * bgra = NULL;
	int type = frame->type & input_frame_type_image_masks;

	if(type == input_frame_type_bgra) bgra = (bgra_image_t *)frame->bgra;
	else if(type == input_frame_type_png || type == input_frame_type_jpeg)
	{
		bgra = bgra_image_init(NULL, frame->width, frame->height, NULL);
		rc = bgra_image_load_data(bgra, frame->data, frame->length);
		if(rc)
		{
			bgra_image_clear(bgra);
			free(bgra);
			bgra = NULL;
		}
	}
	if(bgra)
	{
		caffe_tensor_t * results = NULL;
		
		app_timer_t timer[1];
		double time_elapsed = 0;
		app_timer_start(timer);
		ssize_t count = plugin->predict(plugin, bgra, &results);
		
		time_elapsed = app_timer_stop(timer);
		debug_printf("[INFO]::%s()::time_elapsed=%.3f ms", 
			__FUNCTION__,
			time_elapsed * 1000);
		
		if(count > 0  && p_jresults)
		{
			json_object * jresults = json_object_new_object();
			json_object_object_add(jresults, "model", json_object_new_string("caffe-model"));
			json_object_object_add(jresults, "num_outputs", json_object_new_int((int)count));
			for(ssize_t i = 0; i < count; ++i)
			{
				caffe_tensor_t * result = &results[i];
				assert(result->name);
				
				json_object * joutput = json_object_new_object();
				json_object_object_add(jresults, result->name, joutput);

				json_object * jdims = json_object_new_array();
				json_object_object_add(joutput, "dims", jdims);
				for(int ii = 0; ii < 4; ++ii) json_object_array_add(jdims, json_object_new_int(result->dims[ii]));
				
				size_t size = result->n * result->c * result->h * result->w;
				assert(size > 0);
				
				json_object * jdata = json_object_new_array();
				json_object_object_add(joutput, "data", jdata); 
				for(size_t ii = 0; ii < size; ++ii) json_object_array_add(jdata, json_object_new_double(result->data[ii]));
			}
			*p_jresults = jresults;
			rc = 0;
		}

		if(results) {
			for(int i = 0; i < count; ++i) caffe_tensor_cleanup(&results[i]);
			free(results);
		}
		
		if(bgra != frame->bgra)
		{
			bgra_image_clear(bgra);
			free(bgra);
		}
	}
	return rc;
}

int ann_plugin_init(ai_engine_t * engine, json_object * jconfig)
{
	assert(jconfig);
	caffe_model_plugin_t * plugin = caffe_model_plugin_new(jconfig, engine);
	assert(plugin);

	engine->priv = plugin;
	engine->init = ann_plugin_init;
	engine->cleanup = ai_plugin_caffe_cleanup;
	engine->load_config = ai_plugin_caffe_load_config;
	engine->predict = ai_plugin_caffe_predict;
	return 0;
}


#if defined(_TEST_CAFFE_MODEL) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif


