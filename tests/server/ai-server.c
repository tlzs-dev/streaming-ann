/*
 * ai-server.c
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

#include "ann-plugin.h"
#include "io-input.h"
#include "ai-engine.h"
#include "input-frame.h"

#include "mjpeg-server.h"

#include <glib.h>


#define IO_PLUGIN_DEFAULT 		"io-plugin::input-source"
#define IO_PLUGIN_HTTPD			"io-plugin::httpd"
#define IO_PLUGIN_HTTP_CLIENT	"io-plugin::httpclient"
#define AI_ENGINE_DARKNET		"ai-engine::darknet"

json_object * load_config(const char * conf_file, const char * plugin_type)
{
	json_object * jconfig = NULL;
	if(conf_file) jconfig = json_object_from_file(conf_file);

	if(NULL == jconfig)
	{
		jconfig = json_object_new_object();
		// use default settings
		if(strcasecmp(plugin_type, IO_PLUGIN_DEFAULT) == 0)
		{
			// camera uri:  [rtsp/http/fsnotify/file]://camera_or_video_or_image_path,
			static const char * camera_id = "input1";
			static const char * camera_uri = "rtsp://admin:admin00000@192.168.1.107/Streaming/Channels/101";		// "0": local_camera (/dev/video0)
			
			json_object_object_add(jconfig, "name", json_object_new_string(camera_id));
			json_object_object_add(jconfig, "uri", json_object_new_string(camera_uri));
		}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTPD) == 0)
		{
			json_object_object_add(jconfig, "port", json_object_new_string("9001"));
		}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTP_CLIENT) == 0)
		{
			json_object_object_add(jconfig, "url", json_object_new_string("http://localhost:9001"));
		}else if(strcasecmp(plugin_type, AI_ENGINE_DARKNET) == 0)
		{
			jconfig = json_object_new_object();
			json_object_object_add(jconfig, "conf_file", json_object_new_string("models/yolov3.cfg"));
			json_object_object_add(jconfig, "weights_file", json_object_new_string("models/yolov3.weights"));
		}
	}

	return jconfig;
}

#include <pthread.h>
typedef struct ai_server_context
{
	void * user_data;
	io_input_t * input;
	ai_engine_t * engines[1];
	mjpg_server_t * mjpg;
	
	pthread_mutex_t mutex;
}ai_server_context_t;

int ai_server_on_new_frame(io_input_t * input, const input_frame_t * frame);

void ann_plugins_dump(ann_plugins_helpler_t * helpler)
{
	assert(helpler);
	printf("==== %s(%p):: num_plugins=%ld\n", __FUNCTION__, helpler, (long)helpler->num_plugins);
	ann_plugin_t ** plugins = helpler->plugins;
	for(int i = 0; i < helpler->num_plugins; ++i)
	{
		ann_plugin_t * plugin = plugins[i];
		assert(plugin);

		printf("%.3d: type=%s, filename=%s\n", i, plugin->type, plugin->filename); 
	}
	return;
}

#include <gst/gst.h>
#include <unistd.h>
#include <time.h>

static gboolean on_timeout(ai_server_context_t * ctx);
int main(int argc, char **argv)
{
	gst_init(&argc, &argv);
	static const char * aiengine_type = AI_ENGINE_DARKNET;
	
	int rc = -1;
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, "plugins", NULL);
	assert(helpler);
	ann_plugins_dump(helpler);
	
	ai_server_context_t server[1];
	memset(server, 0, sizeof(server));
	pthread_mutex_init(&server->mutex, NULL);
	
	io_input_t * input = io_input_init(NULL, IO_PLUGIN_DEFAULT, server);
	ai_engine_t * engine = ai_engine_init(NULL, aiengine_type, server);
	mjpg_server_t * mjpg = mjpg_server_init(NULL, "9080", 0, server);
	assert(input && engine && mjpg);

	mjpg->run(mjpg, 1);
	usleep(100);

	server->input = input;
	server->engines[0] = engine;
	server->mjpg = mjpg;
	
	json_object * jinput = load_config(NULL, IO_PLUGIN_DEFAULT);
	json_object * jengine = load_config(NULL, aiengine_type);
	assert(jinput && jengine);

	rc = input->init(input, jinput);		assert(0 == rc);
//	rc = engine->init(engine, jengine);		assert(0 == rc);
	

	input->run(input);
	

	g_timeout_add(5000, (GSourceFunc)on_timeout, server);
	GMainLoop * loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);

	input->stop(input);
	io_input_cleanup(input);
	ai_engine_cleanup(engine);
	return 0;
}

volatile int quit = 0;
static gboolean on_timeout(ai_server_context_t * ctx)
{
	if(quit > 3) return G_SOURCE_REMOVE;

	quit++;
	
	mjpg_server_t * mjpg = ctx->mjpg;
	assert(mjpg);
	io_input_t * input = ctx->input;
	input_frame_t * frame = input_frame_new();

	long frame_number = input->get_frame(input, -1, frame);
	if(frame_number > 0 && frame->data)
	{
		int img_type = frame->type & input_frame_type_image_masks;
		assert(img_type == input_frame_type_jpeg || img_type == input_frame_type_bgra);

		if(img_type == input_frame_type_jpeg)
		{
			mjpg->update_jpeg(mjpg, frame->data, frame->length);
		}else
		{
			mjpg->update_bgra(mjpg, frame->data, frame->width, frame->height, frame->channels);
		}
	}
	
	return G_SOURCE_CONTINUE;
}

int ai_server_on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	ai_server_context_t * ctx = input->user_data;
	mjpg_server_t * mjpg = ctx->mjpg;
	
	bgra_image_t bgra[1];
	memset(bgra, 0, sizeof(bgra));
	int rc = 0;
	int img_type = frame->type & input_frame_type_image_masks;
	if(img_type != input_frame_type_bgra)
	{
		rc = bgra_image_load_data(bgra, frame->data, frame->length);
	}else
	{
		bgra_image_init(bgra, frame->width, frame->height, frame->data);
	}
	char sz_type[200] = "";
	input_frame_type_to_string(img_type, sz_type, sizeof(sz_type));
	printf("image size: %d x %d, type = %d(%s)\n", bgra->width, bgra->height, frame->type, sz_type);
	if(img_type == input_frame_type_jpeg)
	{
		mjpg->update_jpeg(mjpg, frame->data, frame->length);
	}else
	{
		mjpg->update_bgra(mjpg, bgra->data, bgra->width, bgra->height, 4);
	}
	bgra_image_clear(bgra);

	return rc;
}
