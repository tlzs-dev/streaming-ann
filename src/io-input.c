/*
 * io-proxy.c
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

#include <dlfcn.h>

#include "utils.h"
#include "input-frame.h"
#include "io-input.h"
#include "ann-plugin.h"

const unsigned char io_input_magic[8] = { 0x07, 'I', '-', 'p', 'r', 'x', 'y', '\0' };		// for tcp-payload header

enum io_input_type io_input_type_from_string(const char * sz_type)
{
	if(NULL == sz_type) return io_input_type_invalid;
	if(strcasecmp(sz_type, "io-plugin::tcpd") == 0) 	return io_input_type_tcp_server;
	if(strcasecmp(sz_type, "io-plugin::httpd") == 0) 	return io_input_type_http_server;
	if(strcasecmp(sz_type, "io-plugin::tcp") == 0) 		return io_input_type_tcp_client;
	if(strcasecmp(sz_type, "io-plugin::http") == 0) 	return io_input_type_http_client;
	if(strcasecmp(sz_type, "memory") == 0) 				return io_input_type_memory;
	if(strcasecmp(sz_type, "default") == 0) 			return io_input_type_default;
	return io_input_type_invalid;
}

// internal datatype
typedef struct double_buffer double_buffer_t;
struct double_buffer
{
	input_frame_t * frames[2];
	pthread_mutex_t mutex;
	long frame_number;
	long (* set)(double_buffer_t * dbuf, const input_frame_t * frame);
	long (* get)(double_buffer_t * dbuf, input_frame_t * frame);
};

static void double_buffer_free(double_buffer_t * dbuf)
{
	pthread_mutex_lock(&dbuf->mutex);
	input_frame_free(dbuf->frames[0]);
	input_frame_free(dbuf->frames[1]);
	dbuf->frames[0] = NULL;
	dbuf->frames[1] = NULL;
	pthread_mutex_unlock(&dbuf->mutex);
	pthread_mutex_destroy(&dbuf->mutex);
	free(dbuf);
	return;
}

static long double_buffer_set(double_buffer_t * dbuf, const input_frame_t * frame)
{
	if(NULL == dbuf->frames[1]) return -1;
	
	input_frame_copy(dbuf->frames[1], frame);
	pthread_mutex_lock(&dbuf->mutex);

	if(frame->frame_number > 0)
	{
		dbuf->frame_number = frame->frame_number;
	}else
	{
		++dbuf->frame_number;
	}
	long frame_number = dbuf->frame_number;
	
	input_frame_t * tmp = dbuf->frames[0];
	dbuf->frames[0] = dbuf->frames[1];
	dbuf->frames[1] = tmp;
	pthread_mutex_unlock(&dbuf->mutex);
	return frame_number;
}


static long double_buffer_get(double_buffer_t * dbuf, input_frame_t * frame)
{
	if(NULL == dbuf->frames[0]) return -1;
	if(NULL == frame) return dbuf->frame_number;	// query current frame_number only
	
	pthread_mutex_lock(&dbuf->mutex);
	input_frame_copy(frame, dbuf->frames[0]);
	if(dbuf->frame_number > 0)
	{
		frame->frame_number = dbuf->frame_number;
	}
	pthread_mutex_unlock(&dbuf->mutex);
	return frame->frame_number;
}

double_buffer_t * double_buffer_new(void)
{
	double_buffer_t * dbuf = calloc(1, sizeof(*dbuf));
	assert(dbuf);

	int rc = pthread_mutex_init(&dbuf->mutex, NULL);
	assert(0 == rc);

	dbuf->frames[0] = input_frame_new();
	dbuf->frames[1] = input_frame_new();
	
	dbuf->set = double_buffer_set;
	dbuf->get = double_buffer_get;
	return dbuf;
}

/*****************************************************************
 * io_input: member_functions
*****************************************************************/
static long io_input_get_frame(struct io_input * input, long prev_frame, input_frame_t * frame)
{
	if(NULL == input || NULL == input->frame_buffer) return -1;
	long frame_number =  double_buffer_get(input->frame_buffer, frame);
	//~ debug_printf("get_frame: type=%d, size=%d x %d, length=%ld", frame->type, frame->width, frame->height, (long)frame->length);
	return frame_number;
}

static long io_input_set_frame(struct io_input * input, const input_frame_t * frame)
{
	if(NULL == input || NULL == input->frame_buffer) return -1;

	//~ debug_printf("set_frame: type=%d, size=%d x %d, length=%ld", frame->type, frame->width, frame->height, (long)frame->length);
	return double_buffer_set(input->frame_buffer, frame);
}

/*****************************************************************
 * io_input: constructor / desctructor
*****************************************************************/
io_input_t * io_input_init(io_input_t * input, const char * sz_type, void * user_data)
{
	if(NULL == sz_type) sz_type = "io-plugin::input-source";

	ann_plugins_helpler_t * plugins = ann_plugins_helpler_get_default();
	ann_plugin_t * plugin = plugins->find(plugins, sz_type);
	
	if(NULL == plugin) {
		fprintf(stderr, "%s()::unknown input_type '%s'\n", __FUNCTION__, sz_type);
		return NULL;
	}
	
	if(NULL == input) input = calloc(1, sizeof(*input));
	assert(input);

	input->user_data = user_data;

	input->frame_buffer = double_buffer_new();
	assert(input->frame_buffer);

	input->set_frame = io_input_set_frame;
	input->get_frame = io_input_get_frame;

	if(plugin)
	{
		assert(plugin->init_func);
		input->init = plugin->init_func;
	}
	return input;
}

void io_input_cleanup(io_input_t * input)
{
	if(NULL == input) return;
	
	if(input->stop)
	{
		input->stop(input);
	}
	if(input->cleanup)
	{
		input->cleanup(input);
	}
	double_buffer_free(input->frame_buffer);
	return;
}


#if defined(_TEST_IO_PROXY) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif

