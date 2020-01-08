/*
 * test-io-inputs.c
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
#include <gst/gst.h>

#include "ann-plugin.h"
#include "io-input.h"

#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
#define IO_PLUGIN_HTTPD	"io-plugin::httpd"
#define IO_PLUGIN_HTTP_CLIENT	"io-plugin::httpclient"

/* notification::callback when a new frame is available  */
int test_on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	static long frame_number = 0;
	assert(frame && frame->data);
	char sz_type[200] = "";
	ssize_t cb_type = input_frame_type_to_string(frame->type, sz_type, sizeof(sz_type));
	assert(cb_type > 0);

	printf("==== frame_number: %ld ====\n", frame_number++);
	printf("\t: type: %s (0x%.8x)\n", sz_type, frame->type);
	printf("\t: image_size: %d x %d\n", frame->width, frame->height);
	return 0;
}

json_object * load_config(const char * conf_file, const char * plugin_type)
{
	json_object * jconfig = NULL;
	if(conf_file) return json_object_from_file(conf_file);

	jconfig = json_object_new_object();
	assert(jconfig);

	// use default settings
	if(strcasecmp(plugin_type, IO_PLUGIN_DEFAULT) == 0)
	{
		// camera uri:  [rtsp/http/fsnotify/file]://camera_or_video_or_image_path,
		static const char * camera_id = "input1";
		static const char * camera_uri = "0";		// "0": local_camera (/dev/video0)
		
		json_object_object_add(jconfig, "name", json_object_new_string(camera_id));
		json_object_object_add(jconfig, "uri", json_object_new_string(camera_uri));
	}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTPD) == 0)
	{
		json_object_object_add(jconfig, "port", json_object_new_string("9001"));
	}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTP_CLIENT) == 0)
	{
		json_object_object_add(jconfig, "url", json_object_new_string("http://localhost:9001"));
	}
	return jconfig;
}

static const char * plugin_type = IO_PLUGIN_DEFAULT;
static const char * plugins_path = "plugins";
int parse_args(int argc, char ** argv);

volatile int quit;
int main(int argc, char **argv)
{
	int rc = 0;
	gst_init(&argc, &argv);
	parse_args(argc, argv);
	
	ann_plugins_helpler_init(NULL, plugins_path, NULL);
	io_input_t * input = io_input_init(NULL, plugin_type, NULL);
	assert(input);

	
	json_object * jconfig = load_config(NULL, plugin_type);
	rc = input->init(input, jconfig);
	assert(0 == rc);

	/*
	 * input->on_new_frame:  on new_frame available notification callback
	 * 	if this callback is set to NULL,
	 * 	use input->get_frame() to retrieve the lastest frame
	*/ 
	input->on_new_frame = test_on_new_frame; // nullable
	input->run(input);

	if(strcasecmp(plugin_type, IO_PLUGIN_HTTPD) == 0)	// httpd only, patch to libsoup 
	{
		GMainLoop * loop = g_main_loop_new(NULL, TRUE);
		g_main_loop_run(loop);
	}else
	{
		char command[200] = "";
		char * line = NULL;
		while((line = fgets(command, sizeof(command) - 1, stdin)))
		{
			if(line[0] == 'q') break;
			break;
		}
	}

	io_input_cleanup(input);
	return 0;
}

#include <getopt.h>
int parse_args(int argc, char ** argv)
{
	static struct option options[] = {
		{"plugins-dir", required_argument, 0, 'd'},
		{"plugins-type", required_argument, 0, 't'},
		{NULL}
	};
	int option_index = 0;
	while(1)
	{
		int c = getopt_long(argc, argv, "d:t:", options, &option_index);
		if(c == -1) break;

		switch(c)
		{
		case 'd': plugins_path = optarg; break;
		case 't': plugin_type = optarg; break;
		default:
			fprintf(stderr, "invalid args: %c\n", c);
			exit(1);
		}
	}
	return 0;
}

