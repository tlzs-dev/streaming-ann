/*
 * tcp-server.c
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <time.h>
#include <unistd.h>

#include "io-input.h"
#include "input-frame.h"
#include "utils.h"
#include "auto-buffer.h"

#define ANN_PLUGIN_TYPE_STRING "io-plugin::tcpd"

const char * ann_plugin_get_type(void)
{
	return ANN_PLUGIN_TYPE_STRING;
}
#undef ANN_PLUGIN_TYPE_STRING


int io_plugin_init(io_input_t * input, json_object * jconfig)
{
	debug_printf("%s() ...", __FUNCTION__);
	return 0;
}
int io_plugin_run(io_input_t * input)
{
	debug_printf("%s() ...", __FUNCTION__);
	return 0;
}
int io_plugin_stop(io_input_t * input)
{
	debug_printf("%s() ...", __FUNCTION__);
	return 0;
}
int io_plugin_cleanup(io_input_t * input)
{
	debug_printf("%s() ...", __FUNCTION__);
	return 0;
}

static int tcp_server_run(io_input_t * input);

typedef struct tcp_server_private
{
	io_input_t * input;

	char * port;
	int server_fd;
	int efd;

	int quit;
	int is_busy;

	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t th;

	auto_buffer_t in_buf[1];
	input_frame_t frame[1];
	long frame_number;
}tcp_server_private_t;
static void tcp_server_private_free(tcp_server_private_t * priv)
{
	return;
}

static tcp_server_private_t * tcp_server_private_new(io_input_t * input)
{
	tcp_server_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);

	priv->input = input;
	input->priv = priv;

	int rc = 0;
	rc = pthread_mutex_init(&priv->mutex, NULL);	assert(0 == rc);
	rc = pthread_cond_init(&priv->cond, NULL);		assert(0 == rc);
	//~ auto_buffer_init(priv->in_buf);

	
	
	return priv;
}


static int tcp_server_run(io_input_t * input)
{
	debug_printf("%s(%p)...", __FUNCTION__, input);
	
	return 0;
}
static void tcp_server_cleanup(io_input_t * input)
{
	if(input) tcp_server_private_free(input->priv);
	return;
}


void * make_private(io_input_t * input, json_object * jinput)
{
	debug_printf("%s(%p)...", __FUNCTION__, input);
	
	tcp_server_private_t * priv = tcp_server_private_new(input);
	assert(priv && input->priv == priv);

	input->run = tcp_server_run;
	input->cleanup = tcp_server_cleanup;
	
	return priv;
}



#if defined(_TEST_TCP_SERVER) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif

