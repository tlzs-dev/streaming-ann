/*
 * webserver.c
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
#include <libsoup/soup.h>

#include "utils.h"
#include "img_proc.h"
#include "ann-plugin.h"
#include "io-input.h"
#include "ai-engine.h"

//~ static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;

int start_ai_engine(void);

typedef struct webserver_context
{
	io_input_t * input;
	ai_engine_t * engine;
	int quit;
	
	SoupServer * server;
	pthread_mutex_t mutex;
	pthread_t th;
	
}webserver_context_t;
void webserver_context_cleanup(webserver_context_t * ctx)
{
	return;
}

static webserver_context_t g_context[1];
static void on_demo_handler(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);


int main(int argc, char **argv)
{
	webserver_context_t * ctx = g_context;
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, "plugins", NULL);
	assert(helpler);
	
	start_ai_engine();
	GError * gerr = NULL;
	GTlsCertificate * cert = g_tls_certificate_new_from_files(
		"ssl/certs/api.tlzs.co.jp.cert.pem",
		"ssl/keys/api.tlzs.co.jp.key.pem",
		&gerr);
	assert(cert && NULL == gerr);
	

	SoupServer * server = soup_server_new(
		SOUP_SERVER_SERVER_HEADER, "StreammingAnn-Demo", 
		SOUP_SERVER_SSL_CERT_FILE, "ssl/certs/api.tlzs.co.jp.cert.pem",
		SOUP_SERVER_SSL_KEY_FILE, "ssl/keys/api.tlzs.co.jp.key.pem",
		NULL);
	assert(server);
	ctx->server = server;
	
	
	soup_server_add_handler(server, "/tlzs/demo", on_demo_handler, ctx, NULL);
	soup_server_listen_all(server, 8081,
		SOUP_SERVER_LISTEN_HTTPS, &gerr);
	if(gerr)
	{
		fprintf(stderr, "[ERROR]::soup_server_listen_all() failed: %s\n", 
			gerr->message);
		g_error_free(gerr);
		exit(1);
	}
	
	GMainLoop * loop = g_main_loop_new(NULL, FALSE);
	assert(loop);
	g_main_loop_run(loop);
	
	g_main_loop_unref(loop);
	
	webserver_context_cleanup(ctx);
	return 0;
}



int start_ai_engine()
{
	webserver_context_t * ctx = g_context;
	int rc = 0;

	
	json_object * jconfig = json_object_from_file("conf/webserver.json");
	assert(jconfig);
	json_object * jinput = NULL;
	json_object * jengine = NULL;
	json_bool ok = FALSE;
	
	ok = json_object_object_get_ex(jconfig, "input",  &jinput ); assert(ok && jinput);
	ok = json_object_object_get_ex(jconfig, "engine", &jengine); assert(ok && jengine);
	
	const char * input_type = json_get_value(jinput, string, type);
	const char * engine_type = json_get_value(jengine, string, type);
	
	io_input_t * input = io_input_init(NULL, input_type, ctx);
	ai_engine_t * engine = ai_engine_init(NULL, engine_type, ctx);
	assert(input && engine);
	
	//~ rc = input->init(input, jinput);	assert(0 == rc);
	rc = engine->init(engine, jengine);	assert(0 == rc);
	
	ctx->input = input;
	ctx->engine = engine;
	
	//~ input->run(input);
	//~ printf("input: %p, type = %d\n", input, input->type);
	return rc;
}

static void on_get_homepage(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);
static void on_ai_request(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data);

void show_home_page(SoupServer * server, SoupMessage * msg, void * user_data);

static void on_demo_handler(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	if(msg->method == SOUP_METHOD_GET)
	{
		on_get_homepage(server, msg, path, query, client, user_data);
		return;
	}else if(msg->method == SOUP_METHOD_POST || msg->method == SOUP_METHOD_PUT)
	{
		on_ai_request(server, msg, path, query, client, user_data);
		return;
	}else if(msg->method == SOUP_METHOD_HEAD)
	{
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
	return;
}

void show_error_page(SoupServer * server, SoupMessage * msg, const char * err_fmt, ...)
{
	static const char default_error[] = "{\"error_code\": 1, \"error_msg\": \"ERROR\" }\r\n";
	if(NULL == err_fmt)
	{
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_STATIC, default_error, sizeof(default_error) - 1);
	}else
	{
		char err_msg[4096] = "";
		va_list args;
		va_start(args, err_fmt);
		int cb = vsnprintf(err_msg, sizeof(err_msg), err_fmt, args);
		va_end(args);
		if(cb <= 0)
		{
			soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			return;
		}
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, err_msg, cb);
	}
	soup_message_set_status(msg, SOUP_STATUS_OK);
	return;
}

volatile int home_page_is_dirty = 1;		// TODO: add fs-notify support
void show_home_page(SoupServer * server, SoupMessage * msg, void * user_data)
{
	static const char * fmt = "{\"error_code\": %d, \"error_msg\": \"%s\" }\r\n";
	static const char * home_page = "html/index.html";
	static char * html = NULL;
	static long length = 0;
	if(home_page_is_dirty)
	{
		if(html) free(html);
		html = NULL;
		
		FILE * fp = fopen(home_page, "rb");
		assert(fp);
		if(NULL == fp)
		{
			show_error_page(server, msg, fmt, 1001, "'index.html' File Not Found!");
			goto label_cleanup;
		}
		fseek(fp, 0, SEEK_END);
		ssize_t file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		assert(file_size > 0);
		if(file_size <= 0)
		{
			show_error_page(server, msg, fmt, 1002, "invalid file size");
			goto label_cleanup;
		}
		
		html = malloc(file_size + 1);
		if(NULL == html)
		{
			soup_message_set_status(msg, SOUP_STATUS_INSUFFICIENT_STORAGE);
			goto label_cleanup;
		}
		
		length = fread(html, 1, file_size, fp);
		fclose(fp);
		fp = NULL;
		
		if(length != file_size)
		{
			soup_message_set_status(msg, SOUP_STATUS_INSUFFICIENT_STORAGE);
			length = 0;
			goto label_cleanup;
		}
		
		
		
		
	//	home_page_is_dirty = 0; // TODO: add fs-notify support
		
label_cleanup:
		if(fp) fclose(fp);
		if(length <= 0 && html) {
			free(html);
			html = NULL;
		}
	}
	
	
	if(length <= 0 || NULL == html)
	{
		show_error_page(server, msg, NULL);
		return;
	}
	
	soup_message_set_response(msg, "text/html;charset=utf-8", 
			SOUP_MEMORY_COPY, html, length);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	return;
}

static void on_get_homepage(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	show_home_page(server, msg, user_data);
	return;
}

static void on_ai_request(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, void * user_data)
{
	static const char * fmt = "{\"error_code\": %d, \"error_msg\": \"%s\" }\r\n";
	const char * req_content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
	
	printf("Request: Content-Type: %s\n", req_content_type);
	printf("content-length: %ld\n", 
		msg->request_body?(long)msg->request_body->length: (long)0);
	
	if(g_content_type_equals(req_content_type, "application/json"))
	{
		//...
	}else if(g_content_type_equals(req_content_type, "image/jpeg"))
	{
		input_frame_t * frame = input_frame_new();
		assert(frame);
		
		SoupMessageBody * body = msg->request_body;
		assert(body && body->data && body->length > 0 && body->length <= (10 * 1000 * 1000));
		input_frame_set_jpeg(frame, (unsigned char *)body->data, body->length, NULL, 0);
		
		webserver_context_t * ctx = user_data;
		ai_engine_t * engine = ctx->engine;
		assert(ctx && engine);
		json_object * jresult = NULL;
		int rc = engine->predict(engine, frame, &jresult);
		if(rc == 0 && jresult)
		{
			const char * json_str = json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PLAIN);
			assert(json_str);
			int cb_json = strlen(json_str);
			assert(cb_json > 0);
			
			soup_message_set_response(msg, "application/json",
				SOUP_MEMORY_COPY, json_str, cb_json);
			soup_message_set_status(msg, SOUP_STATUS_OK);
		}
		if(jresult) json_object_put(jresult);
		input_frame_free(frame);
		if(0 == rc) return;
	}
	show_error_page(server, msg, fmt, 0, "TODO: ...");
	return;
}
