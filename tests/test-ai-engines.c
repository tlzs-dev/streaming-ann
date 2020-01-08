/*
 * test-ai-engines.c
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

#include <json-c/json.h>
#include "ann-plugin.h"
#include "ai-engine.h"

#include "utils.h"
#include "input-frame.h"

json_object * load_config(const char * conf_file, const char * engine_type)
{
	json_object * jconfig = NULL;
	if(conf_file) jconfig = json_object_from_file(conf_file);

	if(NULL == jconfig)
	{
		jconfig = json_object_new_object();
		json_object_object_add(jconfig, "conf_file", json_object_new_string("models/yolov3.cfg"));
		json_object_object_add(jconfig, "weights_file", json_object_new_string("models/yolov3.weights"));
	}

	return jconfig;
}

static const char * aiengine_type = "ai-engine::darknet";
int main(int argc, char **argv)
{
	int rc = -1;
	assert(ann_plugins_helpler_init(NULL, "plugins", NULL));

	ai_engine_t * engine = ai_engine_init(NULL, aiengine_type, NULL);
	assert(engine);

	json_object * jconfig = load_config(NULL, aiengine_type);
	rc = engine->init(engine, jconfig);
	assert(0 == rc);

	// test
	const char * image_file = "1.jpg";
	input_frame_t * frame = input_frame_new();
	unsigned char * jpeg_data = NULL;
	ssize_t cb_data = load_binary_data(image_file, &jpeg_data);
	assert(jpeg_data && cb_data > 0);
	
	rc = input_frame_set_jpeg(frame, jpeg_data, cb_data, NULL, 0);
	assert(0 == rc && frame->data && frame->width > 0 && frame->height > 0 && frame->length > 0);
	frame->length = frame->width * frame->height * 4;
	
	json_object * jresults = NULL;
	rc = engine->predict(engine, frame, &jresults);
	if(0 == rc)
	{
		fprintf(stderr, "detections: %s\n", json_object_to_json_string_ext(jresults, JSON_C_TO_STRING_PRETTY));
	}

	input_frame_free(frame);
	if(jresults) json_object_put(jresults);
	if(jconfig) json_object_put(jconfig);

	ai_engine_cleanup(engine);
	return 0;
}

