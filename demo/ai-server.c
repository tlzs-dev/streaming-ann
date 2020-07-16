/*
 * ai-server.c
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

#include <pthread.h>
#include <libsoup/soup.h>
#include "ai-engine.h"
#include "ann-plugin.h"
#include "utils.h"

typedef struct global_param
{
	const char * conf_file;
	unsigned int port;
	const char * plugins_dir;
	
	SoupServer * server;
	json_object * jconfig;
	ssize_t count;
	ai_engine_t ** engines;
}global_param_t;
global_param_t * global_param_parse_args(global_param_t * params, int argc, char ** argv);
void global_param_cleanup(global_param_t * params);

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#define global_lock()	pthread_mutex_lock(&g_mutex)
#define global_unlock()	pthread_mutex_unlock(&g_mutex)


void on_request_ai_engine(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	assert(user_data);
	global_param_t * params = user_data;
	
	printf("method: %s\n", msg->method);
	if(msg->method != SOUP_METHOD_POST) 
	{
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	int engine_index = 0;
	if(query)
	{
		const char * sz_index = g_hash_table_lookup(query, "engine");
		if(sz_index)
		{
			engine_index = atoi(sz_index);
			if(engine_index < 0 || engine_index >= params->count) {
				soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
				return;
			}
		}
	}
	ai_engine_t * engine = params->engines[engine_index];
	assert(engine);
	
	const char * content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
	printf("content-type: %s\n", content_type);
	if(!g_content_type_equals(content_type, "image/jpeg"))
	{
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	int rc = input_frame_set_jpeg(frame, 
		(unsigned char *)msg->request_body->data, 
		msg->request_body->length, NULL, 0);
	assert(0 == rc);
	
	json_object * jresult = NULL;
	
	printf("frame: %d x %d\n", frame->width, frame->height);
	global_lock();
	rc = engine->predict(engine, frame, &jresult);
	global_unlock();
	
	printf("rc=%d, jresult=%p\n", rc, jresult);
	
	if(rc || NULL == jresult)
	{
		if(jresult) json_object_put(jresult);
		jresult = json_object_new_object();
		json_object_object_add(jresult, "err_code", json_object_new_int(1));
	}
	
	const char * response = json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PLAIN);
	assert(response);
	int cb = strlen(response);
	
	soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, cb);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	json_object_put(jresult);
	return;
}


static global_param_t g_params[1] = {{
	.conf_file = "ai-server.json",
	.port = 9090,
	.plugins_dir = "plugins",
}};
int main(int argc, char **argv)
{
	global_param_t * params = global_param_parse_args(NULL, argc, argv);
	assert(params && params->count && params->engines);
	
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "ai-server", NULL);
	assert(server);
	params->server = server;
	
	static const char * path = "/ai";
	soup_server_add_handler(server, path, 
		(SoupServerCallback)on_request_ai_engine, params, NULL);
	
	gboolean ok = FALSE;
	GError * gerr = NULL;

	ok = soup_server_listen_all(server, params->port, 0, &gerr);
	if(!ok || gerr)
	{
		fprintf(stderr, "soup_server_listen_all() failed: %s\n", gerr?gerr->message:"unknown error");
		if(gerr) g_error_free(gerr);
		exit(1);
	}
	
	GSList * uris = soup_server_get_uris(server);
	for(GSList * uri = uris; uri; uri = uri->next)
	{
		gchar * sz_uri = soup_uri_to_string(uri->data, FALSE);
		if(sz_uri) {
			printf("listening on: %s%s\n", sz_uri, path);
		}
		soup_uri_free(uri->data);
		uri->data = NULL;
		g_free(sz_uri);
	}
	g_slist_free(uris);
	
	GMainLoop * loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(loop);
	
	g_main_loop_unref(loop);
	global_param_cleanup(params);
	
	return 0;
}



/******************************************************************************
 * global_params
 *****************************************************************************/
#include <getopt.h>
static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: \n"
		   "    %s [--conf=<conf_file.json>] [--port=<port>] [--plugins_dir=<plugins>] \n", argv[0]);
	return;
}
global_param_t * global_param_parse_args(global_param_t * params, int argc, char ** argv)
{
	if(NULL == params) params = g_params;
	static struct option options[] = {
		{"conf", required_argument, 0, 'c' },	// config file
		{"port", required_argument, 0, 'p' },	// AI server listening port
		{"plugins_dir", required_argument, 0, 'd' },	// plugins path
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	unsigned int port = 0;
	const char * plugins_dir = NULL;
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "c:p:d:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 'c': params->conf_file = optarg; 	break;
		case 'p': port = atoi(optarg); break;
		case 'd': plugins_dir = optarg; 	break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	
	// load config
	assert(params->conf_file && params->conf_file[0]);
	json_object * jconfig = json_object_from_file(params->conf_file);
	assert(jconfig);
	params->jconfig = jconfig;
	
	if(0 == port || port > 65535) {
		port = json_get_value_default(jconfig, int, port, params->port);
		params->port = port;
	}
	if(NULL == plugins_dir) {
		plugins_dir = json_get_value_default(jconfig, string, plugins_dir, params->plugins_dir);
		params->plugins_dir = plugins_dir;
	}
	
	// init plugins
	ann_plugins_helpler_init(NULL, plugins_dir, params);
	
	// init ai-engines
	json_object * jai_engines = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "engines", &jai_engines);
	assert(ok && jai_engines);
	
	int count = json_object_array_length(jai_engines);
	assert(count > 0);
	
	ai_engine_t ** engines = calloc(count, sizeof(*engines));
	for(int i = 0; i < count; ++i)
	{
		json_object * jengine = json_object_array_get_idx(jai_engines, i);
		assert(jengine);
		
		const char * plugin_name = json_get_value(jengine, string, plugin_name);
		if(NULL == plugin_name) plugin_name = "ai-engine::darknet";
		ai_engine_t * engine = ai_engine_init(NULL, plugin_name, params);
		assert(engines);
		
		int rc = engine->init(engine, jengine);
		assert(0 == rc);
		
		engines[i] = engine;
	}
	params->count = count;
	params->engines = engines;

	return params;
}

void global_param_cleanup(global_param_t * params)
{
	if(NULL == params) return;
	if(params->count && params->engines)
	{
		ai_engine_t ** engines = params->engines;
		for(ssize_t i = 0; i < params->count; ++i)
		{
			if(engines[i]) {
				ai_engine_cleanup(engines[i]);
				free(engines[i]);
				engines[i] = NULL;
			}
		}
		free(engines);
		params->engines = NULL;
		params->count = 0;
	}
	if(params->jconfig) json_object_put(params->jconfig);
	params->jconfig = NULL;
}
