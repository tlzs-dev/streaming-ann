/*
 * darknet.c
 * 
 * Copyright 2020 chehw <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "ai-engine.h"
#include "utils.h"

#include "input-frame.h"

#include "darknet.h"				// original library header, libdarknet.so / libdarknet.a
#include "darknet-wrapper.h"

#define AI_PLUGIN_TYPE_STRING "ai-engine::darknet"

/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {
#endif
const char * ann_plugin_get_type(void);
int ann_plugin_init(ai_engine_t * engine, json_object * jconfig);

#define ai_plugin_darknet_init ann_plugin_init

#ifdef __cplusplus
}
#endif


const char * ann_plugin_get_type(void)
{
	return AI_PLUGIN_TYPE_STRING;
}

static void ai_plugin_darknet_cleanup(struct ai_engine * engine)
{
	return;
}
static int ai_plugin_darknet_load_config(struct ai_engine * engine, json_object * jconfig)
{
	return 0;
}
static int ai_plugin_darknet_predict(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults)
{
	debug_printf("%s(): frame: type=%d, size=%d x %d", __FUNCTION__,
		frame->type,
		frame->width, frame->height);
		
	int rc = -1;
	darknet_context_t * darknet = engine->priv;
	assert(darknet);

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
			bgra = NULL;
		}
	}
	if(bgra)
	{
		ai_detection_t * results = NULL;
		
		app_timer_t timer[1];
		double time_elapsed = 0;
		app_timer_start(timer);
		ssize_t count = darknet->predict(darknet, bgra, &results);
		
		time_elapsed = app_timer_stop(timer);
		debug_printf("[INFO]::darknet->predict()::time_elapsed=%.3f ms", 
			time_elapsed * 1000);
		
		if(count > 0  && p_jresults)
		{
			json_object * jresults = json_object_new_object();
			json_object_object_add(jresults, "model", json_object_new_string("darknet::YOLOV3"));
			
			json_object * jdetections = json_object_new_array();
			json_object_object_add(jresults, "detections", jdetections);

			for(ssize_t i = 0; i < count; ++i)
			{
				json_object * jdet = json_object_new_object();
				json_object_object_add(jdet, "class", json_object_new_string(results[i].class_names));
				json_object_object_add(jdet, "class_index", json_object_new_int(results[i].klass));
				json_object_object_add(jdet, "confidence", json_object_new_double(results[i].confidence));
				json_object_object_add(jdet, "left", json_object_new_double(results[i].x));
				json_object_object_add(jdet, "top", json_object_new_double(results[i].y));
				json_object_object_add(jdet, "width", json_object_new_double(results[i].cx));
				json_object_object_add(jdet, "height", json_object_new_double(results[i].cy));

				json_object_array_add(jdetections, jdet);
			}
			*p_jresults = jresults;
			rc = 0;
		}

		if(results) free(results);
		if(bgra != frame->bgra)
		{
			bgra_image_clear(bgra);
			free(bgra);
		}
	}
	return rc;
}

static int ai_plugin_darknet_update(struct ai_engine * engine, const ai_tensor_t * truth)
{
	return 0;
}
static int ai_plugin_darknet_get_property(struct ai_engine * engine, const char * name, void ** p_value)
{
	return 0;
}
static int ai_plugin_darknet_set_property(struct ai_engine * engine, const char * name, const void * value, size_t length)
{
	return 0;
}

int ann_plugin_init(ai_engine_t * engine, json_object * jconfig)
{
	static const char * cfg_file = "models/yolov3.cfg";
	static const char * weights_file = "models/yolov3.weights";
	//~ static const char * labels_file = "conf/coco.names";
	
	if(NULL == jconfig) {
		jconfig = json_object_new_object();
		json_object_object_add(jconfig, "conf_file", json_object_new_string(cfg_file));
		json_object_object_add(jconfig, "weights_file", json_object_new_string(weights_file));
	}
	assert(jconfig);
	
	darknet_context_t * darknet = darknet_context_new(jconfig, engine);
	assert(darknet);

	engine->priv = darknet;
	engine->init = ai_plugin_darknet_init;
	engine->cleanup = ai_plugin_darknet_cleanup;
	engine->load_config = ai_plugin_darknet_load_config;
	engine->predict = ai_plugin_darknet_predict;
	engine->update = ai_plugin_darknet_update;
	engine->get_property = ai_plugin_darknet_get_property;
	engine->set_property = ai_plugin_darknet_set_property;
	return 0;
}

#undef AI_PLUGIN_TYPE_STRING
