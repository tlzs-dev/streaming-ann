/*
 * http-server.c
 * 
 * Copyright 2019 chehw <htc.chehw@gmail.com>
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
#include <libsoup/soup.h>

#include "utils.h"
#include "io-input.h"
#include "input-frame.h"
#include "auto-buffer.h"


#define ANN_PLUGIN_TYPE_STRING "io-plugin::httpd"
/* Entry-Point Functions */
#ifdef __cplusplus
extern "C" {
#endif
const char * ann_plugin_get_type(void);
int ann_plugin_init(io_input_t * input, json_object * jconfig);

#ifdef __cplusplus
}
#endif

/* plugin-private */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct http_session
{
	void * user_data;		// struct io_plugin_httpd * 
	SoupServer * server;
	SoupMessage * msg;
	SoupClientContext * client;
	
	char * path;
	GHashTable * query;

	int async_mode;
	pthread_t th;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	
	auto_buffer_t in_buf[1];
	auto_buffer_t out_buf[1];
}http_session_t;

static http_session_t * http_session_new(SoupServer * server, SoupMessage * msg,
	const char * path, GHashTable * query,
	SoupClientContext * client, void * user_data);
static void http_session_free(http_session_t * session);

typedef struct io_plugin_httpd
{
	void * user_data;
	json_object * jconfig;
	io_input_t * input;

	GMainContext * ctx;
	GMainLoop * loop;
	SoupServer * server;
	char * port;
	int local_only;
	
	char * path;
	pthread_t th;
	pthread_mutex_t mutex;

	//~ input_frame_t * frame_buffer[2];
	int status;		// 0: init; 1: running; 2: paused; -1: error
	int quit;
}io_plugin_httpd_t;
io_plugin_httpd_t * io_plugin_httpd_new(io_input_t * input, void * user_data);
void io_plugin_httpd_private_free(io_plugin_httpd_t * httpd);

// server callbacks
static void on_server_callback(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, gpointer user_data);

// virtual interfaces
#define io_plugin_httpd_init 		ann_plugin_init
#define io_plugin_httpd_run			io_plugin_run
#define io_plugin_httpd_stop		io_plugin_stop
#define io_plugin_httpd_cleanup 	io_plugin_cleanup
static int io_plugin_httpd_load_config(io_input_t * httpd, json_object * jconfig);

#define MAX_HTTP_SESSION_BUFFER_SIZE	(1 << 24)	// (16 MBytes)

#ifdef __cplusplus
}
#endif

static int io_plugin_httpd_get_property(struct io_input * input, const char * name, char ** p_value, size_t * p_length)
{
	// TODO: ...
	return -1;
}

static int io_plugin_httpd_set_property(io_input_t * input, const char * name, const char * value, size_t cb_value)
{
	int rc = -1;
	if(NULL == name || NULL == value) return -1;
	io_plugin_httpd_t * httpd = input->priv;
	assert(httpd);
	pthread_mutex_lock(&httpd->mutex);

	// TODO: set property
	
	pthread_mutex_unlock(&httpd->mutex);
	return rc;
}

static int io_plugin_httpd_load_config(io_input_t * input, json_object * jconfig)
{
	debug_printf("%s() ...", __FUNCTION__);
	io_plugin_httpd_t * httpd = input->priv;

	httpd->jconfig = json_object_get(jconfig);
	
	const char * port = json_get_value_default(jconfig, string, port, "9001");
	const char * path = json_get_value_default(jconfig, string, path, "/");
	int local_only = json_get_value(jconfig, int, local_only);
	
	if(port) httpd->port = strdup(port);
	if(path) httpd->path = strdup(path);
	httpd->local_only = local_only;

	return 0;
	 
}


static void * io_plugin_httpd_process(void * user_data)
{
	int rc = -1;
	io_plugin_httpd_t * httpd = user_data;
	assert(httpd);

	GMainLoop * loop = httpd->loop;

	//~ const char * path = httpd->path;
	//~ const char * port = httpd->port;
	//~ int local_only = httpd->local_only;

	//~ if(NULL == port) port = "9001";
	//~ GError * gerr = NULL;
	//~ gboolean ok = FALSE;
	//~ SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "io-proxy-httpd", NULL);
	//~ assert(server);
	

	//~ if(NULL == path) path = "/";
	//~ // printf("path: '%s', jconfig: %p\n", path, jconfig);
	
	//~ if(local_only)
	//~ {
		//~ ok = soup_server_listen_local(server, atoi(port), 0, &gerr);
	//~ }
	//~ else
	//~ {
		//~ ok = soup_server_listen_all(server, atoi(port), 0, &gerr);
	//~ }
	//~ soup_server_add_handler(server, path, on_server_callback, httpd, NULL);
	
	//~ if(!ok || gerr)
	//~ {
		//~ fprintf(stderr, "[ERROR]::%s()::soup_server_listen_all() failed: %s\n",
			//~ __FUNCTION__, gerr->message);
		//~ g_error_free(gerr);
		//~ g_object_unref(server);

		
		//~ httpd->status = 0;
		//~ pthread_exit((void *)(long)rc);
	//~ }

	//~ GSList * uris = soup_server_get_uris(server);
	//~ assert(uris);

	//~ for(GSList * uri = uris; NULL != uri; uri = uri->next)
	//~ {
		//~ fprintf(stderr, "[INFO]::" ANN_PLUGIN_TYPE_STRING "::Listening on %s, path=%s\n",
			//~ soup_uri_to_string(uri->data, FALSE),
			//~ path
			//~ );
		//~ soup_uri_free(uri->data);
	//~ }
	//~ g_slist_free(uris);
	
	//~ httpd->server = server;
	rc = 0;
	
	if(httpd->status < 1)
	{
		httpd->status = 1;
		
		if(NULL == loop)
		{
					
			GMainContext * ctx = g_main_context_new();
			loop = g_main_loop_new(ctx, FALSE);
			assert(loop);
			httpd->loop = loop;
		}

		debug_printf("%s()::server is running.", __FUNCTION__);
		g_main_loop_run(loop);

		debug_printf("%s()::server stopped.", __FUNCTION__);
		g_main_loop_unref(loop);
		httpd->loop = NULL;
	}

	httpd->status = 0;
	pthread_exit((void *)(long)rc);
}


static int io_plugin_run(io_input_t * input)
{
	debug_printf("%s() ...", __FUNCTION__);
	io_plugin_httpd_t * httpd = input->priv;
	assert(httpd && httpd->input == input);

	httpd->quit = 0;
	
	return 0;
	
	int rc = pthread_create(&httpd->th, NULL, io_plugin_httpd_process, httpd);
//	usleep(10);		// sleep 10 micro-seconds: waiting for httpd_thread init final
	return rc;
}

static int io_plugin_stop(io_input_t * input)
{
	io_plugin_httpd_t * httpd = input->priv;
	assert(httpd && httpd->input == input);

	if(httpd->th)
	{
		debug_printf("%s() ...", __FUNCTION__);
		
		pthread_mutex_lock(&httpd->mutex);
		httpd->quit = 1;
		httpd->status = 2;	// pause server
		if(httpd->loop)
		{
			g_main_loop_quit(httpd->loop);
		}
		pthread_mutex_unlock(&httpd->mutex);
		void * exit_code = NULL;
		
		int rc = pthread_join(httpd->th, &exit_code);

		fprintf(stderr, "%s(%d)::%s()::io_proxy_httpd_thread exited with code %ld, rc=%d\n",
			__FILE__, __LINE__, __FUNCTION__,
			(long)exit_code, rc);
		httpd->th = (pthread_t)0;
	}
	return 0;
}

static void io_plugin_cleanup(io_input_t * input)
{
	debug_printf("%s() ...", __FUNCTION__);
	io_plugin_stop(input);
	
	io_plugin_httpd_t * httpd = input->priv;
	io_plugin_httpd_private_free(httpd);
	return;
}


/****************************************************
 * Implementations
****************************************************/
io_plugin_httpd_t * io_plugin_httpd_new(io_input_t * input, void * user_data)
{
	assert(input && input->init);
	
	io_plugin_httpd_t * httpd = calloc(1, sizeof(*httpd));
	assert(httpd);
	httpd->user_data = user_data;
	httpd->input = input;

	input->get_property = io_plugin_httpd_get_property;
	input->set_property = io_plugin_httpd_set_property;

	input->run 		= io_plugin_httpd_run;
	input->stop 	= io_plugin_httpd_stop;
	input->cleanup 	= io_plugin_httpd_cleanup;
	input->load_config = io_plugin_httpd_load_config;

	pthread_mutex_init(&httpd->mutex, NULL);
	return httpd;
}

void io_plugin_httpd_private_free(io_plugin_httpd_t * httpd)
{
	if(httpd->path) free(httpd->path);
	if(httpd->port) free(httpd->port);

	if(httpd->loop)
	{
		g_main_loop_quit(httpd->loop);
		g_main_loop_unref(httpd->loop);
		httpd->loop = NULL;
	}

	if(httpd->server)
	{
		g_object_unref(httpd->server);
		httpd->server = NULL;
	}

	free(httpd);
	return;
}



/*******************************************************
 * http session
*******************************************************/
static http_session_t * http_session_new(SoupServer * server, SoupMessage * msg,
	const char * path, GHashTable * query,
	SoupClientContext * client, void * user_data)
{
	assert(server && msg && path && user_data);
	http_session_t * session = calloc(1, sizeof(*session));
	assert(session);

	session->user_data = user_data;	// httpd
	session->server = server;
	session->msg = msg;
	session->path = strdup(path);
	if(query) session->query = g_hash_table_ref(query);
	session->client = client;
	
	pthread_mutex_init(&session->mutex, NULL);
	auto_buffer_init(session->in_buf, 0);
	auto_buffer_init(session->out_buf, 0);

	return session;
}

static const char no_error[] = "{ \"error-code\": 0, \"message\": \"\" }";
static int on_get(http_session_t * session);
static int on_post(http_session_t * session);
static void * http_session_process(http_session_t * session)	// response: json format
{
	assert(session);

	int rc = -1;
	int async_mode = session->async_mode;

	debug_printf("%s(%p)::path=%s, async_mode=%d", 
		__FUNCTION__, session, session->path,
		async_mode
		);
	SoupServer * server = session->server;
	SoupMessage * msg = session->msg;

	assert(server && msg);
	if(msg->method == SOUP_METHOD_GET)
	{
		rc = on_get(session);
	}else
	{
		rc = on_post(session);
	}

	
	if(async_mode)
	{
		soup_server_unpause_message(server, msg);
		
		io_plugin_httpd_t * httpd = session->user_data;
		assert(httpd && httpd->server == server);
		
		GMainContext * ctx = httpd->ctx;
		assert(ctx);
		if(g_main_context_acquire(ctx))
		{
			g_main_context_dispatch(ctx);
			g_main_context_release(ctx);
		}
		
	}
	
	if(async_mode) pthread_exit((void *)(long)rc);
	http_session_free(session);
	return ((void *)(long)rc);
}

static int on_get(http_session_t * session)
{
	int rc = -1;
	io_plugin_httpd_t * httpd = session->user_data;
	assert(httpd);
	SoupMessage * msg = session->msg;
	assert(msg);
	
	io_input_t * input = httpd->input;
	assert(input && input->get_frame);
	input_frame_t * frame = input_frame_new();
	long frame_number = input->get_frame(input, -1, frame);
	if(frame_number > 0)
	{
		json_object * jresponse = json_object_new_object();

		char * image_b64 = NULL;
		ssize_t cb_image_b64 = 0;
		assert(frame->length > 0);
		cb_image_b64 = base64_encode(frame->data, frame->length, &image_b64);
		assert(cb_image_b64 && image_b64);
		json_object * jimage = json_object_new_string_len(image_b64, cb_image_b64);
		
		json_object_object_add(jresponse, "image", jimage);
		json_object_object_add(jresponse, "type",json_object_new_int(frame->type));
		json_object_object_add(jresponse, "frame_number", json_object_new_int64(frame->frame_number));
		
		const char * response = json_object_to_json_string_ext(jresponse, JSON_C_TO_STRING_PRETTY);
		if(response)
		{
			soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY,
				response, strlen(response));
			soup_message_set_status(msg, SOUP_STATUS_OK);
			rc = 0;
		}
		free(image_b64);
		json_object_put(jresponse);
	}

	input_frame_free(frame);
	if(rc) soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
	return rc;
}

static int on_post(http_session_t * session)
{
	int rc = -1;
	io_plugin_httpd_t * httpd = session->user_data;
	assert(httpd);
	
//	SoupServer * server = session->server;
	SoupMessage * msg = session->msg;
	const char * path = session->path;
	GHashTable * query = session->query;
	assert(msg && path);
	
	
	SoupMessageHeaders * req_hdr = msg->request_headers;
	SoupMessageBody * request = msg->request_body;
	SoupMessageHeaders * hdr = msg->response_headers;
	SoupMessageBody * body = msg->response_body;

	const char * format = NULL;
	if(query)
	{
		format = g_hash_table_lookup(query, "format");
		printf("format: %s\n", format);
	}

	assert(req_hdr);
	GHashTable * params = NULL;
	const char * content_type = soup_message_headers_get_content_type(req_hdr,
		&params);

	printf("content-type: %s\n", content_type);
	

	io_input_t * input = httpd->input;
	assert(input);
	
	input_frame_t * frame = input_frame_new();

	//~ SoupBuffer * in_buf = soup_message_body_flatten(request);
	//~ assert(in_buf);

	//~ const unsigned char * image_data = NULL;
	const unsigned char * image_data = (unsigned char *)request->data;
	//~ gsize length = 0;
	//~ soup_buffer_get_data(in_buf, &image_data, &length);
	//~ assert(image_data && length <= request->length);

	rc = -1;
	if(g_content_type_equals(content_type, "image/jpeg"))
	{
		rc = input_frame_set_jpeg(frame, (unsigned char *)image_data, request->length, NULL, 0);
	}else if(g_content_type_equals(content_type, "image/png"))
	{
		rc = input_frame_set_png(frame, (unsigned char *)image_data, request->length, NULL, 0);
	}else if(strcasecmp(content_type, "image/bgra") == 0 && params)
	{
		const char * sz_width = g_hash_table_lookup(params, "width");
		const char * sz_height = g_hash_table_lookup(params, "height");
		const char * sz_channels = g_hash_table_lookup(params, "channels");
		const char * sz_stride = g_hash_table_lookup(params, "stride");

		int width = sz_width?atoi(sz_width):-1;
		int height = sz_height?atoi(sz_height):-1;
		int stride = 0;
		int channels = 4;

		if(width > 0 && height > 0)
		{
			if(sz_channels) channels = atoi(sz_channels);
			stride = sz_stride?atoi(sz_stride):(width * channels);
		}

		assert((stride * height) == request->length);
		rc = input_frame_set_bgra(frame,
			&(bgra_image_t){.data = (unsigned char *)image_data, .width = width, .height = height, .channels = channels, .stride = stride },
			NULL, 0);
	}

	if(0 == rc)
	{
		if(input->set_frame) input->set_frame(input, frame);
		if(input->on_new_frame) input->on_new_frame(input, frame);
	}
	
	if(params) g_hash_table_unref(params);
	

	soup_message_headers_set_content_type(hdr, "application/json", NULL);

	if(NULL == frame->meta_data && NULL == frame->json_str)
	{
	//	soup_message_body_append(body, SOUP_MEMORY_COPY, no_error, sizeof(no_error) - 1);
	}else
	{
		assert(frame->json_str);	// TODO: add meta_data support
		soup_message_body_append(body, SOUP_MEMORY_COPY, frame->json_str, frame->cb_json);
	}
	
	input_frame_free(frame);
	soup_message_set_status(msg, SOUP_STATUS_OK);
	return rc;
}


static int http_session_run_async(http_session_t * session)
{
	int rc = 0;
	assert(session && session->server && session->msg && session->path);
	session->async_mode = 1;
	rc = pthread_create(&session->th, NULL, (void * (*)(void *))http_session_process, session);

	if(0 == rc)
	{
		pthread_detach(session->th);
	}
	//~ rc = (int)(long)http_session_process(session);
	
	return rc;
}

static void http_session_free(http_session_t * session)
{
	if(NULL == session) return;
	free(session->path);
	if(session->query) g_hash_table_unref(session->query);

	pthread_mutex_destroy(&session->mutex);
	free(session);
	return;
}


/*******************************************************
 * http server callback
*******************************************************/

static inline void append_cors_headers(SoupMessageHeaders * hdr)
{
	soup_message_headers_append(hdr, "Access-Control-Allow-Origin", "*");
	soup_message_headers_append(hdr, "Access-Control-Allow-Headers", "*");

	return;
}

static void on_server_callback(SoupServer * server, SoupMessage * msg, const char * path, GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	debug_printf("%s():: path=%s", __FUNCTION__, path);
	io_plugin_httpd_t * httpd = user_data;
	assert(httpd && httpd->server == server && httpd->path);

	int cb_server_path = strlen(httpd->path);

	debug_printf("path: %s, query: %p, httpd->path=%s", path, query, httpd->path);
	/* if invalid url */
	if( (strstr(path, httpd->path) != path) 
		|| (path[cb_server_path] != '\0' && path[cb_server_path] != '/' )
	)	
	{
		soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
		return;
	}

	/* add CORS support */
	if(msg->method == SOUP_METHOD_OPTIONS)
	{
		append_cors_headers(msg->response_headers);
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}

	/* valid methods: POST PUT */
	if(msg->method == SOUP_METHOD_POST || msg->method == SOUP_METHOD_PUT || msg->method == SOUP_METHOD_GET)
	{
		// verify post data && length
		SoupMessageBody * request = msg->request_body;
		if(NULL == request || request->length >= MAX_HTTP_SESSION_BUFFER_SIZE)
		{
			soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
			return;
		}

		http_session_t * session = http_session_new(server, msg, path, query, client, user_data);
		if(NULL == session)
		{
			soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
			return;
		}
		soup_server_pause_message(server, msg);
		http_session_run_async(session);
		
		return;
	}

	soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
	return;
}


/*******************************************************
 * DLL Entry-Point Functions
*******************************************************/
const char * ann_plugin_get_type(void)
{
	return ANN_PLUGIN_TYPE_STRING;
}

int ann_plugin_init(io_input_t * input, json_object * jconfig)
{
	debug_printf("%s() ...", __FUNCTION__);
	io_plugin_httpd_t * httpd = io_plugin_httpd_new(input, input->user_data);
	assert(httpd);
	input->priv = httpd;

	httpd->jconfig = jconfig;
	if(jconfig)
	{
		int rc = io_plugin_httpd_load_config(input, jconfig);
		if(rc)
		{
			io_plugin_httpd_private_free(httpd);
			return -1;
		}
	}
	
	GMainContext * ctx = g_main_context_new();
	GMainLoop * loop = g_main_loop_new(ctx, FALSE);
	assert(loop);
	httpd->ctx  =ctx;
	httpd->loop = loop;
	
	const char * path = httpd->path;
	const char * port = httpd->port;
	int local_only = httpd->local_only;

	if(NULL == port) port = "9001";
	GError * gerr = NULL;
	gboolean ok = FALSE;
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "io-proxy-httpd", NULL);
	assert(server);
	

	if(NULL == path) path = "/";
	// printf("path: '%s', jconfig: %p\n", path, jconfig);
	
	if(local_only)
	{
		ok = soup_server_listen_local(server, atoi(port), 0, &gerr);
	}
	else
	{
		ok = soup_server_listen_all(server, atoi(port), 0, &gerr);
	}
	soup_server_add_handler(server, path, on_server_callback, httpd, NULL);
	
	if(!ok || gerr)
	{
		fprintf(stderr, "[ERROR]::%s()::soup_server_listen_all() failed: %s\n",
			__FUNCTION__, gerr->message);
		g_error_free(gerr);
		g_object_unref(server);

		
		httpd->status = 0;
	//	pthread_exit((void *)(long)rc);
		return -1;
	}

	GSList * uris = soup_server_get_uris(server);
	assert(uris);

	for(GSList * uri = uris; NULL != uri; uri = uri->next)
	{
		fprintf(stderr, "[INFO]::" ANN_PLUGIN_TYPE_STRING "::Listening on %s, path=%s\n",
			soup_uri_to_string(uri->data, FALSE),
			path
			);
		soup_uri_free(uri->data);
	}
	g_slist_free(uris);
	
	httpd->server = server;
	
			
	return 0;
}

/*******************************************************/



#undef IO_PROXY_TYPE_STRING

#if defined(_TEST_HTTP_SERVER) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif




