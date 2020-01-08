/*
 * mjpg-server.c
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

#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

#include "img_proc.h"

#include "utils.h"
#include "mjpeg-server.h"


#define MJPG_STREAMING_BOUNDARY	"mjpgstreamingd557f22382ef97de"

#define MJPG_STREAMING_COMMON_HDR	"HTTP/1.1 200 OK\r\n"	\
	"Connection: close\r\n" \
    "Server: MJPG-Streaming/chehw.v.0.2.0\r\n" \
    "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
    "Pragma: no-cache\r\n" \
    "Expires: Mon, 3 Jan 2000 00:00:00 GMT\r\n"	\
    "Content-Type: multipart/x-mixed-replace;boundary=" MJPG_STREAMING_BOUNDARY	"\r\n"				\
    "Access-Control-Allow-Origin: *\r\n"	\
    "\r\n"			\
    "--" MJPG_STREAMING_BOUNDARY "\r\n"


#define MJPG_STREAMING_PARTIAL_HDR_FMT	"Content-Type: image/jpeg\r\n"	\
	"Content-Length: %ld\r\n" \
	"X-Timestamp: %d.%06d\r\n" \
	"\r\n"

#define MJPG_STREAMING_PARTIAL_EOL	"\r\n--" MJPG_STREAMING_BOUNDARY "\r\n"
#define EOL_SIZE	(sizeof(MJPG_STREAMING_PARTIAL_EOL) - 1)


#ifndef log_printf
#define log_printf(fmt, ...) do {														\
		struct timespec ts[1];															\
		clock_gettime(CLOCK_REALTIME, ts);												\
		struct tm t[1];																	\
		localtime_r(&ts->tv_sec, t);													\
		char sz_time[200] = "";															\
		ssize_t cb = strftime(sz_time, sizeof(sz_time), "%Y%m%d %H:%M:%S", t);			\
		assert(cb > 0);																	\
		snprintf(sz_time + cb, sizeof(sz_time) - cb, " %.9ld", (long)ts->tv_nsec);		\
		fprintf(stderr, "%s " "\e[33m" "%s(%d)::%s():" fmt "\e[39m" "\n",				\
			sz_time, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__);					\
	}while(0)
#endif


//~ typedef struct mjpg_server
//~ {
	//~ http_server_t http[1];
	//~ void * priv;
	
	//~ int (* run)(struct mjpg_server * mjpg, int async_mode);
	//~ int (* stop)(struct mjpg_server * mjpg);
	//~ int (* on_request(struct mjpg_server * mjpg);		// can be overrided

	//~ long frame_number;
	
	//~ // push mode
	//~ int (* update_jpeg)(struct mjpg_server * mjpg, const unsigned char * jpeg_data, size_t length);
	//~ int (* update_bgra)(struct mjpg_server * mjpg, const unsigned char * bgra_data, int width, int height, int channels);

	//~ // pull mode
	//~ int (* need_data)(struct mjpg_server * mjpg);		// virtual callback, need override 
//~ }mjpg_server_t;

typedef struct jpeg_buffer
{
	unsigned char * data;
	ssize_t length;
	struct timespec timestamp[1];
}jpeg_buffer_t;

static void jpeg_buffer_clear(jpeg_buffer_t * jbuf)
{
	if(NULL == jbuf) return;
	free(jbuf->data);
	jbuf->data = NULL;
	jbuf->length = 0;
}

static void jpeg_buffer_set(jpeg_buffer_t * jbuf, const unsigned char * data, ssize_t length, struct timespec * timestamp)
{
	assert(jbuf);
	if(NULL == timestamp)
	{
		clock_gettime(CLOCK_MONOTONIC, jbuf->timestamp);
	}else
	{
		memcpy(jbuf->timestamp, timestamp, sizeof(jbuf->timestamp));
	}
	
	jbuf->data = realloc(jbuf->data, length);
	assert(jbuf);

	memcpy(jbuf->data, data, length);
	jbuf->length = length;
	return;
}


typedef struct mjpg_server_private
{
	mjpg_server_t * server;
	pthread_mutex_t mutex;			// http response mutex
	pthread_cond_t cond;

	pthread_mutex_t buffer_mutex;	// jpeg_buffer mutex
	
	int async_mode;
	pthread_t th;

	struct timespec timestamp[1];
	long frame_number;

	int is_busy;

	jpeg_buffer_t * buffers[2];		// double buffer
	
	long (* get_data)(struct mjpg_server_private * priv, unsigned char ** p_data, ssize_t * p_length, struct timespec * timestamp);
	long (* set_data)(struct mjpg_server_private * priv, const unsigned char * data, ssize_t length, struct timespec * timestamp);
}mjpg_server_private_t;

static long mjpg_server_private_get_data(struct mjpg_server_private * priv, unsigned char ** p_data, ssize_t * p_length, struct timespec * timestamp)
{
	assert(priv && p_data && p_length);
	
	pthread_mutex_lock(&priv->buffer_mutex);
	jpeg_buffer_t * jpeg = priv->buffers[0];
	assert(jpeg && jpeg->data && jpeg->length > 0);

	assert(p_data);
	unsigned char * data = *p_data;
	data = realloc(data, jpeg->length);
	assert(data);

	*p_data = data;
	memcpy(data, jpeg->data, jpeg->length);
	*p_length = jpeg->length;

	if(timestamp)
	{
		memcpy(timestamp, jpeg->timestamp, sizeof(jpeg->timestamp));
	}
	
	pthread_mutex_unlock(&priv->buffer_mutex);
	return priv->frame_number;
}

static long mjpg_server_private_set_data(struct mjpg_server_private * priv, const unsigned char * data, ssize_t length, struct timespec * timestamp)
{
	assert(priv && data && length > 0);
	assert(priv->buffers[1]);

	jpeg_buffer_set(priv->buffers[1], data, length, timestamp);

	// swap buffer
	pthread_mutex_lock(&priv->buffer_mutex);
	jpeg_buffer_t * tmp = priv->buffers[1];
	priv->buffers[1] = priv->buffers[0];
	priv->buffers[0] = tmp;
	pthread_mutex_unlock(&priv->buffer_mutex);

	pthread_mutex_lock(&priv->mutex);

	//log_printf("\e[31msignal...\e[39m");
	if(!priv->is_busy) pthread_cond_signal(&priv->cond);
	pthread_mutex_unlock(&priv->mutex);
	
	return ++priv->frame_number;
}

mjpg_server_private_t * mjpg_server_private_new(mjpg_server_t * server)
{
	int rc = 0;
	mjpg_server_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);

	priv->buffers[0] = calloc(1, sizeof(*priv->buffers[0]));
	priv->buffers[1] = calloc(1, sizeof(*priv->buffers[1]));

	priv->server = server;
	rc = pthread_mutex_init(&priv->mutex, NULL);
	assert(0 == rc);

	rc = pthread_mutex_init(&priv->buffer_mutex, NULL);
	assert(0 == rc);
	
	rc = pthread_cond_init(&priv->cond, NULL);
	assert(0 == rc);

	priv->get_data = mjpg_server_private_get_data;
	priv->set_data = mjpg_server_private_set_data;
	return priv;
}

void mjpg_server_private_free(mjpg_server_private_t * priv)
{
	if(NULL == priv) return;
	mjpg_server_t * server = priv->server;
	if(server) server->stop(server);


	int rc = 0;
	void * exit_code = NULL;

	server->http->quit = 1;
	pthread_cond_broadcast(&priv->cond);

	if(priv->th)
	{
		rc = pthread_join(priv->th, &exit_code);
		UNUSED(rc);
		UNUSED(exit_code);

		priv->th = (pthread_t)0;
	}

	pthread_mutex_destroy(&priv->mutex);
	pthread_cond_destroy(&priv->cond);

	

	pthread_mutex_lock(&priv->buffer_mutex);

	jpeg_buffer_clear(priv->buffers[0]);
	jpeg_buffer_clear(priv->buffers[1]);
	
	free(priv->buffers[0]);
	free(priv->buffers[1]);
	priv->buffers[0] = NULL;
	priv->buffers[1] = NULL;
	pthread_mutex_unlock(&priv->buffer_mutex);
	
	pthread_mutex_destroy(&priv->buffer_mutex);
	free(priv);
	return;
}


static void * mjpg_server_process(void * user_data)
{
	log_printf("");
	mjpg_server_t * server = user_data;
	http_server_t * http = user_data;
	mjpg_server_private_t * priv = server->priv;
	assert(priv);

	pthread_mutex_lock(&priv->mutex);

	while(1)
	{
		int rc = pthread_cond_wait(&priv->cond, &priv->mutex);

		printf("cond wait=%d, is_busy=%d\n", rc, priv->is_busy);
		if(rc != 0) break;		// server error
	//	if(http->quit) break;	// http server was stopped by app-thread
		
		unsigned char * jpeg_data = NULL;
		ssize_t jpeg_length = 0;
		struct timespec timestamp[1];
		memset(timestamp, 0, sizeof(timestamp));
		
		long frame_number = priv->get_data(priv, &jpeg_data, &jpeg_length, timestamp);
		printf("frame_number: %ld\n", frame_number);
		
		if(frame_number <= 0) continue;	// empty buffer

		priv->is_busy = 1;
		//~ pthread_mutex_unlock(&priv->mutex);		// unlock mutex to accept new signals
		
	//	pthread_mutex_lock(&http->mutex);
		http_session_t ** sessions = http->sessions;

		if(timestamp->tv_sec == 0)	
		{
			clock_gettime(CLOCK_MONOTONIC, timestamp);
		}

		debug_printf("%s()::on new frame(), sessions_count = %d", (int)http->sessions_count);
		for(size_t i = 0; i < http->sessions_count && !http->quit; ++i)
		{
			http_session_t * session = sessions[i];
			if(session->fd <= 0) continue;		// TODO: garbarge collection	(deadlock check)
			enum http_stage stage = session->request_hdr->stage;
			if(stage < http_stage_request_final || stage >= http_stage_cleanup) continue;

			

			http_auto_buffer_t * out_buf = session->out_buf;
			if(stage < http_stage_response_final)	// common header not sent
			{
				out_buf->push(out_buf, MJPG_STREAMING_COMMON_HDR, sizeof(MJPG_STREAMING_COMMON_HDR) - 1);
				stage = session->request_hdr->stage = http_stage_response_final;
			}

			if(jpeg_data && jpeg_length > 0 && frame_number >= server->frame_number)	// new frame available
			{
				char partial_hdr[PATH_MAX] = "";
				ssize_t cb = snprintf(partial_hdr, sizeof(partial_hdr), MJPG_STREAMING_PARTIAL_HDR_FMT,
					(long)jpeg_length,
					(int)timestamp->tv_sec,
					(int)(timestamp->tv_nsec / 100));
				out_buf->push(out_buf, partial_hdr, cb);
				out_buf->push(out_buf, jpeg_data, jpeg_length);
				out_buf->push(out_buf, MJPG_STREAMING_PARTIAL_EOL, EOL_SIZE);
				session->on_response(session);		// enable write-end
			}
		}
		
		server->frame_number = frame_number;	// update frame_number
	//	pthread_mutex_unlock(&http->mutex);
		if(jpeg_data) free(jpeg_data);


		//~ pthread_mutex_lock(&priv->mutex);		// lock mutex before cond_wait
		priv->is_busy = 0;
	}

	pthread_mutex_unlock(&priv->mutex);

	if(priv->async_mode)
	{
		pthread_exit((void *)(long)0);
	}
	return (void *)(long)0;
}


static int mjpg_server_run(struct mjpg_server * mjpg, int async_mode)
{
	int rc = 0;
	mjpg_server_private_t * priv = mjpg->priv;
	assert(priv);

	http_server_t * http = mjpg->http;
	http->run(http, 1);

	priv->async_mode = async_mode;
	if(async_mode)
	{
		rc = pthread_create(&priv->th, NULL, mjpg_server_process, mjpg);
		usleep(100);
	}else
	{
		rc = (int)(long)mjpg_server_process(mjpg);
	}
	return rc;
}

static int mjpg_server_stop(struct mjpg_server * mjpg)
{
	mjpg->http->quit = 1;
	return 0;
}

static int mjpg_server_on_request(http_session_t * session)
{
	http_header_t * req_hdr = session->request_hdr;
	http_header_t * resp_hdr = session->response_hdr;

	assert(req_hdr &&  resp_hdr);
	enum http_stage stage = req_hdr->stage;

	if(stage >= http_stage_request_final)
	{
		return session->on_error(session, session->user_data);
	}
	
//	req_hdr->stage = http_stage_request_final;
	
	log_printf("req: stage=%d\n", stage);
	printf("method: %s\n", req_hdr->method);
	printf("path: %s\n", req_hdr->path);
	printf("content-length: %ld\n", req_hdr->content_length);
	
	printf("buf=%s\n", (char *)session->in_buf->data);
	

	http_auto_buffer_t * out_buf = session->out_buf;
	http_auto_buffer_reset(out_buf);

	req_hdr->stage = http_stage_request_final;
//~ static const char * default_response = "HTTP/1.1 200 OK\r\n"
	//~ "Content-Type: text/html\r\n"
	//~ "Content-Length: 10\r\n"
	//~ "\r\n"
	//~ "0123456789"
	//~ ;
	
	//~ out_buf->push(out_buf, default_response, strlen(default_response));
	
	return session->on_response(session);
	
	return 0;
}

// push mode
static int mjpg_server_update_jpeg(struct mjpg_server * mjpg, const unsigned char * jpeg_data, size_t length)
{
	mjpg_server_private_t * priv = mjpg->priv;
	assert(priv);

	priv->set_data(priv, jpeg_data, length, NULL);
	return 0;
}

static int mjpg_server_update_bgra(struct mjpg_server * mjpg, const unsigned char * bgra_data, int width, int height, int channels)
{
	unsigned char * jpeg_data = NULL;
	ssize_t length = 0;

	bgra_image_t bgra[1] = {{
		.data = (unsigned char *)bgra_data,
		.width = width,
		.height = height,
		.channels = 4,
	}};
	length = bgra_image_to_jpeg_stream(bgra, &jpeg_data, 95);
	assert(length > 0);

	debug_printf("jpg_data: %p, length = %ld\n", jpeg_data, (long)length);

	mjpg->update_jpeg(mjpg, jpeg_data, length);
	free(jpeg_data);

	
	return 0;
}

// pull mode
static int mjpg_server_need_data(struct mjpg_server * mjpg)		// virtual callback, need override
{
	return -1;
}

mjpg_server_t * mjpg_server_init(mjpg_server_t * mjpg, const char * port, int local_only, void * user_data)
{
	if(NULL == mjpg) mjpg = calloc(1, sizeof(*mjpg));
	assert(mjpg);

	http_server_init(mjpg->http, NULL, port, local_only);
	mjpg->http->user_data = user_data;

	mjpg->update_jpeg = mjpg_server_update_jpeg;
	mjpg->update_bgra = mjpg_server_update_bgra;
	mjpg->need_data = mjpg_server_need_data;

	mjpg->run = mjpg_server_run;
	mjpg->stop = mjpg_server_stop;
	
	mjpg->http->on_request = mjpg_server_on_request;

	mjpg_server_private_t * priv = mjpg_server_private_new(mjpg);
	assert(priv);
	mjpg->priv = priv;
	
	return mjpg;
}
void mjpg_server_cleanup(mjpg_server_t * mjpg)
{
	return;
}



#if defined(_TEST) && defined(_STAND_ALONE)
#include <cairo/cairo.h>

volatile int quit;
int main(int argc, char **argv)
{
	mjpg_server_t * mjpg = mjpg_server_init(NULL, "8081", 0, NULL);
	assert(mjpg);

	srand(12345);
	mjpg->run(mjpg, 1);

	cairo_surface_t * surface = cairo_image_surface_create(
		CAIRO_FORMAT_ARGB32,
		640, 480);
	assert(surface);

	unsigned char * bgra_data = cairo_image_surface_get_data(surface);
	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);

	int fps = 2;

	FILE * fp = fopen("1.jpg", "rb");
	assert(fp);

	unsigned char * jpg_data = NULL;
	ssize_t size = 0;
	ssize_t file_len = 0;
	fseek(fp, 0, SEEK_END);
	file_len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	jpg_data = malloc(file_len);
	assert(jpg_data);
	
	size = fread(jpg_data, 1, file_len, fp);
	fclose(fp);

	assert(size == file_len);

	long frame_number = 0;
	
	while(!quit)
	{	
		cairo_t * cr = cairo_create(surface);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);

		cairo_set_font_size(cr, 18);
		cairo_select_font_face(cr, "Droid Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		
		double x = rand() % 100;
		double y = rand() % 100;

		double cx = rand() % 300;
		double cy = rand() % 300;

		double r = (double)(rand() % 256) / 255.0;
		double g = (double)(rand() % 256) / 255.0;
		double b = (double)(rand() % 256) / 255.0;

		int fill_mode = rand() % 2;
		double line_size = rand() % 10 + 1;
		cairo_set_source_rgb(cr, r, g, b);
		cairo_set_line_width(cr, line_size);

		cairo_rectangle(cr, x, y, cx, cy);
		if(fill_mode)
		{
			cairo_fill_preserve(cr);
		}

		cairo_stroke(cr);

		char title[200] = "";
		snprintf(title, sizeof(title), "frame_num: %ld", ++frame_number);
		cairo_move_to(cr, 10, 30);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_show_text(cr, title);
		cairo_destroy(cr);

		//~ mjpg->update_jpeg(mjpg, jpg_data, size);

		mjpg->update_bgra(mjpg, bgra_data, width, height, 4);
		usleep(1000000 / fps);
	}

	mjpg->stop(mjpg);
	mjpg_server_cleanup(mjpg);
	return 0;
}
#endif

