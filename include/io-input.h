#ifndef _IO_PROXY_H_
#define _IO_PROXY_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "input-frame.h"
#include <json-c/json.h>

enum io_input_type
{
	io_input_type_invalid = -1,
	io_input_type_default = 0,		// struct input-source
	io_input_type_memory = 1,		
	io_input_type_tcp_server,		// passive mode: listening user's inputs: tcp send
	io_input_type_http_server,		// passive mode: listening user's inputs: HTTP POST
	io_input_type_tcp_client,		// pull-mode, GET data from ip:port
	io_input_type_http_client,		// pull-mode, GET data from url
	io_input_type_custom = 999,		// custom plugins
	io_input_types_count
};

enum io_input_type io_input_type_from_string(const char * sz_type);

typedef struct io_input
{
	void * user_data;			// user's context
	void * priv;				// io-plugin's private data
	void * plugins_helpler;		// io_plugins_helpler_t
	
	enum io_input_type type;	// build-in
	char * input_type;			//
	char * name;				// input-id
	
	// virtual interfaces: ( should be overided by io-plugin implementations )
	int (* init)(struct io_input * input, json_object * jconfig);
	int (* run)(struct io_input * input);
	int (* stop)(struct io_input * input);
	void (* cleanup)(struct io_input * input);
	int (* load_config)(struct io_input * input, json_object * jconfig);	// reload config
	int (* get_property)(struct io_input * input, const char * name, char ** p_value, size_t * p_length);
	int (* set_property)(struct io_input * input, const char * name, const char * value, size_t cb_value);
	
	// io_input::member_functions
	long (* get_frame)(struct io_input * input, long prev_frame, input_frame_t * frame);
	long (* set_frame)(struct io_input * input, const input_frame_t * frame);
	
	// user-defined callbacks
	int (* on_new_frame)(struct io_input * input, const input_frame_t * frame);

	// private data ( KEEP UNTOUCHED )
	void * frame_buffer;
}io_input_t;

io_input_t * io_input_init(io_input_t * input, const char * sz_type, void * user_data);
void io_input_cleanup(io_input_t * input);

#ifdef __cplusplus
}
#endif
#endif
