/*
 * default-plugin.c
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

#include <pthread.h>
#include <json-c/json.h>

#include "utils.h"
#include "io-input.h"
#include "input-frame.h"
#include "auto-buffer.h"

#include "input-source.h"
#include <gst/gst.h>

#define ANN_PLUGIN_TYPE_STRING "io-plugin::input-source"

/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {
#endif
const char * ann_plugin_get_type(void);
int ann_plugin_init(io_input_t * input, json_object * jconfig);

#ifdef __cplusplus
}
#endif

const char * ann_plugin_get_type(void)
{
	return ANN_PLUGIN_TYPE_STRING;
}

/****************************************
 * Global Init
****************************************/
static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
//~ static pthread_mutexattr_t s_mutexattr_recursive;
static void init_plugin_context(void)
{
	static int argc = 1;
	static char * args[] = { "io_plugin_input_source.so", NULL };

	char ** argv = args;
	gst_init(&argc, &argv);

	return;
}

// input-source callback function
static int io_plugin_input_source_on_new_frame(struct input_source * priv, const input_frame_t * frame)
{
	int rc = -1;
	io_input_t * input = priv->user_data;
	if(input->set_frame) rc = input->set_frame(input, frame);
	if(input->on_new_frame) rc = input->on_new_frame(input, frame);
	return rc;
}

/****************************************
 * io_plugin-input_source
****************************************/
static int io_plugin_input_source_load_config(io_input_t * input, json_object * jconfig)
{
	if(NULL == jconfig) return -1;
	input_source_t * priv = input->priv;
	assert(priv);
	
	const char * uri = json_get_value(jconfig, string, uri);
	return priv->set_uri(priv, uri);
}


static int io_plugin_get_property(struct io_input * input, const char * name, char ** p_value, size_t * p_length)
{
	// TODO: ...
	
	return -1;
}

static int io_plugin_set_property(io_input_t * input, const char * name, const char * value, size_t cb_value)
{
	int rc = -1;
	if(NULL == name || NULL == value) return -1;
	input_source_t * priv = input->priv;

	if(priv && strcasecmp(name, "uri") == 0 && value)
	{
		priv->set_uri(priv, value);
	}
	
	return rc;
}

static int io_plugin_run(io_input_t * input)
{
	input_source_t * priv = input->priv;
	return priv->play(priv);
}

static int io_plugin_stop(io_input_t * input)
{
	input_source_t * priv = input->priv;
	return priv->stop(priv);
}

static void io_plugin_cleanup(io_input_t * input)
{
	input_source_t * priv = input->priv;
	if(priv)
	{
		input_source_cleanup(priv);
	}
	return;
}

int ann_plugin_init(io_input_t * input, json_object * jconfig)
{
	pthread_once(&s_once_key, init_plugin_context);
	
	input_source_t * priv = input_source_new(input);
	assert(priv);

	priv->on_new_frame = io_plugin_input_source_on_new_frame;
	input->priv = priv;

	input->run = io_plugin_run;
	input->stop = io_plugin_stop;
	
	input->cleanup = io_plugin_cleanup;
	input->load_config = io_plugin_input_source_load_config;

	input->get_property = io_plugin_get_property;
	input->set_property = io_plugin_set_property;

	if(jconfig)
	{
		input->load_config(input, jconfig);
	}
	
	return 0;
	
}
