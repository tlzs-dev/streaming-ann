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

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#define global_lock()	pthread_mutex_lock(&g_mutex)
#define global_unlock()	pthread_mutex_unlock(&g_mutex)


void on_request_ai_engine(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	assert(user_data);
	ai_engine_t * engine = user_data;
	printf("method: %s\n", msg->method);
	
	if(msg->method != SOUP_METHOD_POST) 
	{
		soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
		return;
	}
	
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

int main(int argc, char **argv)
{
	int rc = 0;
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "ai-server", NULL);
	ann_plugins_helpler_init(NULL, "plugins", server);
	ai_engine_t * engine = ai_engine_init(NULL, "ai-engine::darknet", server);
	json_object * jconfig = json_object_new_object();
	json_object_object_add(jconfig, "conf_file", json_object_new_string("models/yolov3.cfg"));
	json_object_object_add(jconfig, "weights_file", json_object_new_string("models/yolov3.weights"));
	
	rc = engine->init(engine, jconfig);
	assert(0 == rc);
	
	static const char * path = "/ai";
	soup_server_add_handler(server, path, 
		(SoupServerCallback)on_request_ai_engine, engine, NULL);
	
	gboolean ok = FALSE;
	GError * gerr = NULL;

	ok = soup_server_listen_all(server, 9090, SOUP_SERVER_LISTEN_IPV4_ONLY, &gerr);
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
	
	ai_engine_cleanup(engine);
	return 0;
}

