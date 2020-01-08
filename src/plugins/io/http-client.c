/*
 * http-client.c
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
#include <curl/curl.h>

#include <glib.h>
#include <gio/gio.h>

#include "utils.h"
#include "io-input.h"
#include "input-frame.h"
#include "auto-buffer.h"


#define ANN_PLUGIN_TYPE_STRING "io-plugin::httpclient"

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

/* plugin-private */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct io_plugin_http_client
{
	void * user_data;
	json_object * jconfig;
	io_input_t * input;
	
	int direction;	// 0: get_data, 1: post_data
	double fps;

	char * url;
	char * content_type;
	int use_ssl;
	int verify_host;

	int (* send_request)(struct io_plugin_http_client * client);
	int (* on_response)(struct io_plugin_http_client * client, CURL * curl, auto_buffer_t * in_buf);
	
	pthread_t th;		// plugin thread
	
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int quit;
	int is_running;
	int is_busy;
	long frame_number;

	CURL * curl;
	//~ CURLcode status;
	//~ long response_code;
	//~ GMainLoop * loop;
	
	auto_buffer_t in_buf[1];

}io_plugin_http_client_t;
static io_plugin_http_client_t * io_plugin_http_client_new(io_input_t * input, void * user_data);
static void io_plugin_http_client_private_free(io_plugin_http_client_t * client);

// client callbacks
static size_t on_data_available(void * data, size_t size, size_t n, void * user_data);

// virtual interfaces
#define io_plugin_http_client_init 			ann_plugin_init
#define io_plugin_http_client_run			io_plugin_run
#define io_plugin_http_client_stop			io_plugin_stop
#define io_plugin_http_client_cleanup 		io_plugin_cleanup
static int io_plugin_http_client_load_config(io_input_t * input, json_object * jconfig);


#ifdef __cplusplus
}
#endif

/****************************************
 * Global Init
****************************************/
static pthread_once_t s_once_key = PTHREAD_ONCE_INIT;
static pthread_mutexattr_t s_mutexattr_recursive;
static void init_plugin_context(void)
{
	CURLcode ret = curl_global_init(CURL_GLOBAL_DEFAULT);
	assert(ret == CURLE_OK);

	int rc = pthread_mutexattr_init(&s_mutexattr_recursive);
	assert(0 == rc);

	//~ rc = pthead_mutexattr_setpshared(&s_mutexattr_recursive, PTHREAD_PROCESS_SHARED);
	rc = pthread_mutexattr_settype(&s_mutexattr_recursive, PTHREAD_MUTEX_RECURSIVE);
	assert(0 == rc);
	return;
}




/****************************************
 * io_plutin_http_client
****************************************/
static int io_plugin_http_client_load_config(io_input_t * input, json_object * jconfig)
{
	if(NULL == jconfig) return -1;

	io_plugin_http_client_t * client = input->priv;
	assert(client);

	char * url = json_get_value(jconfig, string, url);
	int use_ssl = json_get_value(jconfig, int, use_ssl);
	int verify_host = json_get_value_default(jconfig, int, verify_host, 1);
	int direction = json_get_value(jconfig, int, direction);
	double fps = json_get_value(jconfig, double, fps);

	client->url = url;
	client->use_ssl = use_ssl;
	client->verify_host = verify_host;
	client->direction = direction;
	client->fps = fps;
	
	return 0;
}

static int io_plugin_http_client_on_response(struct io_plugin_http_client * client, CURL * curl, auto_buffer_t * in_buf)
{
	//~ debug_printf("%s()::response=%s\n", __FUNCTION__,
		//~ jresponse?json_object_to_json_string_ext(jresponse, JSON_C_TO_STRING_SPACED):"(null)");

	if(client->direction != 0)
	{
		// TODO: check result
		int cb = 0;
		if(in_buf)
		{
			cb = (int)in_buf->length;
			if(cb > 100) cb = 100;

			debug_printf("%s()::response=%.*s\n", __FUNCTION__,
				cb, (char *)in_buf->data);
		}

		return 0;
	}
	
	char * content_type = NULL;
	double content_length = 0;
	CURLcode ret = 0;

	ret = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
	ret = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);

	UNUSED(ret);

	debug_printf("%s()::content_type: %s\r\ncontent-length: %.0f\r\ndata_length=%ld",
		__FUNCTION__,
		content_type, content_length,
		(long)in_buf->length);
	printf("in_buf: %.*s ...\n", (int)20, (char *)in_buf->data);

	io_input_t * input = client->input;
	input_frame_t * frame = input_frame_new();
	long frame_number = 0;

	if(strcasecmp(content_type, "application/json") == 0)
	{
		json_tokener * jtok = json_tokener_new();
		json_object * jresponse = json_tokener_parse_ex(jtok, (char *)in_buf->data + in_buf->cur_pos, in_buf->length);
		enum json_tokener_error jerr = json_tokener_get_error(jtok);

		if(NULL == jresponse || jerr != json_tokener_success)
		{
			fprintf(stderr, "%s()::json_tokener_parse() failed: %s\n",
				__FUNCTION__,
				json_tokener_error_desc(jerr));
			json_tokener_free(jtok);
			if(jresponse) json_object_put(jresponse);
			input_frame_free(frame);
			return -1;
		}

		frame_number = json_get_value_default(jresponse, int, frame_number, 0);
		const char * image_b64 = json_get_value(jresponse, string, image);
		const char * text = json_get_value(jresponse, string, text);
		int cb_text = 0;
		if(text) cb_text = strlen(text);
		
		if(image_b64)
		{
			content_type = NULL;
			char sz_image_type[100] = "";
			
			const char * sub_type = NULL;
			int is_base64 = 0;

			const char * sz_image = image_b64;
			if(strncasecmp(image_b64, "data:", 5) == 0)
			{
				content_type = (char *)image_b64 + 5;
				sub_type = strchr(content_type, ';');
				assert(sub_type);

				size_t cb_type = sub_type - content_type;
				assert(cb_type < sizeof(sz_image_type));
				strncpy(sz_image_type, content_type, cb_type);
				++sub_type;

				is_base64 = (strncasecmp(sub_type, "base64,", sizeof("base64,") - 1) == 0);
				assert(is_base64);
				sz_image = sub_type + sizeof("base64,") - 1;
			}

			ssize_t cb_image = 0;
			unsigned char * image_data = NULL;
			
			cb_image = base64_decode(sz_image, -1, &image_data);
			assert(cb_image > 0);

			gboolean uncertain = TRUE;
			gchar * sz_type = NULL;
			sz_type = g_content_type_guess(NULL, image_data, cb_image, &uncertain);
			if(!uncertain && sz_type)
			{
				int is_png = 0;
				int is_jpeg = g_content_type_equals(sz_type, "image/jpeg");
				if(!is_jpeg) is_png = g_content_type_equals(sz_type, "image/png");

				assert(is_jpeg || is_png);
				if(is_jpeg) 		input_frame_set_jpeg(frame, image_data, cb_image, text, cb_text);
				else if(is_png) 	input_frame_set_png(frame, image_data, cb_image, text, cb_text);
				assert(frame->data);

				if(frame_number > 0) frame->frame_number = frame_number;
				
				if(frame->frame_number > 0 && frame->frame_number != client->frame_number)
				{
					fprintf(stderr, "\e[32m" "%s()::set_frame(frame_number = %ld): %p, size=%dx%d, type=%d" "\e[39m" "\n",
						__FUNCTION__,
						frame_number,
						frame,
						frame->width, frame->height,
						frame->type);

					
				}
			}
			if(sz_type) g_free(sz_type);
		}

		json_tokener_free(jtok);
		if(jresponse) json_object_put(jresponse);
	}else if(strcasecmp(content_type, "image/jpeg") == 0)
	{
		input_frame_set_jpeg(frame, in_buf->data, in_buf->length, NULL, 0);
		frame_number = client->frame_number + 1;

	}else if(strcasecmp(content_type, "image/png") == 0)
	{
		input_frame_set_png(frame, in_buf->data, in_buf->length, NULL, 0);
		frame_number = client->frame_number + 1;
	}

	if(frame && frame->data && frame->length > 0 && client->frame_number != frame_number)
	{
		client->frame_number = frame_number;
		if(input->set_frame) input->set_frame(input, frame);
		if(input->on_new_frame) input->on_new_frame(input, frame);
	}
	if(frame) input_frame_free(frame);
	return 0;
}

static int io_plugin_http_client_send_request(io_plugin_http_client_t * client)
{
	debug_printf("%s()::quit=%d, url=%s", __FUNCTION__, client->quit, client->url);
	assert(client && client->input);
	
	long frame_number = -1;
	io_input_t * input = client->input;
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));

	CURL * curl = client->curl;
	if(NULL == curl)
	{
		curl = curl_easy_init();
		assert(curl);

		client->curl = curl;
	}
	
	curl_easy_reset(curl);

	curl_easy_setopt(curl, CURLOPT_URL, client->url);
	if(client->use_ssl)
	{
		curl_easy_setopt(curl, CURLOPT_USE_SSL, client->use_ssl);
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, client->verify_host);
	}

	struct curl_slist * headers = NULL;
	if(client->direction == 0)	// input-mode: http get
	{
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_data_available);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, client);
		
	}else // output-mode: http post
	{
		frame_number = input->get_frame(input, -1, frame);
		if(frame_number <= 0) {
			input_frame_clear(frame);
			return -1;
		}
		assert((frame->type & input_frame_type_image_masks) == input_frame_type_jpeg);

		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		headers = curl_slist_append(headers, "Content-Type: image/jpeg");
		headers = curl_slist_append(headers, "Accept: *");
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, frame->data);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, frame->length);
	}
	
	auto_buffer_reset(client->in_buf);
	CURLcode ret = curl_easy_perform(curl);
	if(CURLE_OK == ret)
	{
		long response_code = -1;
		ret = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		assert(ret == CURLE_OK);
		debug_printf("response_code: %ld\n", response_code);
		if(response_code == 200)
		{
			client->on_response(client, curl, client->in_buf);
		}
	}else
	{
		debug_printf("curl_easy_perform() failed(%d): %s\n", ret, curl_easy_strerror(ret));
		curl_easy_reset(curl);
	}

	return 0;
}


static int io_plugin_http_client_get_property(struct io_input * input, const char * name, char ** p_value, size_t * p_length)
{
	int rc = -1;
	if(NULL == name || NULL == p_value) return -1;
	io_plugin_http_client_t * client = input->priv;
	assert(client);
	
	pthread_mutex_lock(&client->mutex);
	if(strcasecmp(name, "url") == 0 && client->url)
	{
		*p_value = strdup(client->url);
		if(p_length) *p_length = strlen(client->url);
	}else
	{
		// TODO: ...
	}
	pthread_mutex_unlock(&client->mutex);
	
	
	return rc;
}

static int io_plugin_http_client_set_property(io_input_t * input, const char * name, const char * value, size_t cb_value)
{
	int rc = -1;
	if(NULL == name || NULL == value) return -1;
	io_plugin_http_client_t * client = input->priv;
	assert(client);
	pthread_mutex_lock(&client->mutex);

	if(strcasecmp(name, "url") == 0)
	{
		if(client->url) free(client->url);
		client->url = NULL;

		if(cb_value > 0)
		{
			client->url = malloc(cb_value + 1);
			assert(client->url);
			memcpy(client->url, value, cb_value);
			client->url[cb_value] = '\0';
		}else
		{
			client->url = strdup(value);
		}
		rc = 0;
		pthread_cond_signal(&client->cond);
		
	}

	pthread_mutex_unlock(&client->mutex);
	return rc;
}


static void io_plugin_http_client_private_free(io_plugin_http_client_t * client)
{
	if(NULL == client) return;

	CURL * curl = client->curl;
	client->curl = NULL;

	if(curl) curl_easy_cleanup(curl);
	
	return;
}


static void * worker_thread(void * user_data)
{
	int rc = 0;
	io_plugin_http_client_t * client = user_data;
	assert(client && client->input);
	
	pthread_mutex_lock(&client->mutex);
	while(!client->quit)
	{
		rc = pthread_cond_wait(&client->cond, &client->mutex);
		if(rc) break;
		client->is_busy = 1;

		pthread_mutex_unlock(&client->mutex);
		rc = client->send_request(client);

		client->is_busy = 0;
		pthread_mutex_lock(&client->mutex);
	}

	debug_printf("%s()::thread exited with code %d", __FUNCTION__, rc);
	
	pthread_mutex_unlock(&client->mutex);
	pthread_exit((void *)(long)rc);
}


static gboolean on_timeout(io_plugin_http_client_t * client)
{
	debug_printf("%s()::quit=%d", __FUNCTION__, client->quit);
	if(client->quit || client->fps == 0) return G_SOURCE_REMOVE;

	
	pthread_mutex_lock(&client->mutex);
	if(client->fps > 0 && !client->is_busy)
	{
		pthread_cond_signal(&client->cond);
	}
	pthread_mutex_unlock(&client->mutex);
	return G_SOURCE_CONTINUE;
}

static void * http_client_process(void * user_data)
{
	int rc = 0;
	io_plugin_http_client_t * client = user_data;
	assert(client);
	GMainContext * ctx = g_main_context_new();
	GMainLoop * loop = g_main_loop_new(ctx, FALSE);
	assert(ctx && loop);


	pthread_t worker;	// worker thread (curl)
	rc = pthread_create(&worker, NULL, worker_thread, client);
	assert(0 == rc);
	usleep(10);

	CURL * curl = curl_easy_init();
	assert(curl);

	client->curl = curl;

	double fps = client->fps;
	client->is_running = 1;
	
	if(fps > 0 && fps <= 60)
	{
		g_timeout_add(1000 / fps, (GSourceFunc)on_timeout, client); 
	}else
	{
		pthread_mutex_lock(&client->mutex);
		pthread_cond_signal(&client->cond);
		pthread_mutex_unlock(&client->mutex);
	}

	
	g_main_loop_run(loop);

	client->is_running = 0;
	
	g_main_loop_unref(loop);
	g_main_context_unref(ctx);

	void * exit_code = NULL;

	client->quit = 1;
	pthread_cond_broadcast(&client->cond);
	rc = pthread_join(worker, &exit_code);
	debug_printf("%s()::worker_thread exited with code %ld\n", __FUNCTION__, (long)exit_code);
	
	pthread_exit((void *)(long)rc);
}

int io_plugin_run(io_input_t * input)
{
	assert(input && input->priv);
	debug_printf("%s(%p)...", __FUNCTION__, input);

	int rc = 0;
	io_plugin_http_client_t * client = input->priv;
	assert(client && client->input == input);

	if(!client->is_running)
	{
		rc = pthread_create(&client->th, NULL, http_client_process, client);
		if(rc)
		{
			client->is_running = 0;
		}
	}
	return rc;
}

int io_plugin_stop(io_input_t * input)
{
	debug_printf("%s(%p)...", __FUNCTION__, input);
	return 0;
}

void io_plugin_cleanup(io_input_t * input)
{
	debug_printf("%s(%p)...", __FUNCTION__, input);

	io_plugin_http_client_private_free(input->priv);
	return;
}



static io_plugin_http_client_t * io_plugin_http_client_new(io_input_t * input, void * user_data)
{
	assert(input && input->init);

	int rc = 0;
	io_plugin_http_client_t * client = calloc(1, sizeof(*client));
	assert(client);

	client->input = input;
	client->user_data = user_data;

	auto_buffer_init(client->in_buf, 0);

	client->send_request = io_plugin_http_client_send_request;
	client->on_response = io_plugin_http_client_on_response;
	
	rc = pthread_mutex_init(&client->mutex, &s_mutexattr_recursive);	assert(0 == rc);
	rc = pthread_cond_init(&client->cond, NULL);						assert(0 == rc);

	input->run  = io_plugin_http_client_run;
	input->stop = io_plugin_http_client_stop;
	input->cleanup = io_plugin_http_client_cleanup;
	input->load_config = io_plugin_http_client_load_config;
	input->set_property = io_plugin_http_client_set_property;
	input->get_property = io_plugin_http_client_get_property;

	return client;
}

int ann_plugin_init(io_input_t * input, json_object * jconfig)
{
	pthread_once(&s_once_key, init_plugin_context);
	
	debug_printf("%s(%p)...", __FUNCTION__, input);
	assert(input);
	int rc = 0;
	io_plugin_http_client_t * client = input->priv;
	if(NULL == client)
	{
		client = io_plugin_http_client_new(input, input->user_data);
		assert(client);

		input->priv = client;
	}
	
	if(jconfig)
	{
		rc = input->load_config(input, jconfig);
	}
	return rc;
}
	

static size_t on_data_available(void * data, size_t size, size_t n, void * user_data)
{
	io_plugin_http_client_t * client = user_data;
	auto_buffer_t * in_buf = client->in_buf;

	ssize_t length = size * n;
	assert(length > 0);
	
	ssize_t cb = auto_buffer_push_data(in_buf, data, length);
	if(cb < length) return 0;
	
	return length;
}


