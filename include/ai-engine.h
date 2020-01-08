#ifndef _AI_NETWORK_H_
#define _AI_NETWORK_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <json-c/json.h>
#include "input-frame.h"

typedef struct int_dim4
{
	int n, c, h, w;
}int_dim4;

enum ai_tensor_data_type
{
	ai_tensor_data_type_unknown = -1,
	ai_tensor_data_type_float32 = 0,
	ai_tensor_data_type_float64,
	ai_tensor_data_type_uint8,
	ai_tensor_data_type_uint16,
	ai_tensor_data_type_uint32,
	ai_tensor_data_type_uint64,
	ai_tensor_data_types_count,

	ai_tensor_data_type_masks = 0x7fff,
	ai_tensor_data_type_gpu_flags = 0x8000,
};

typedef struct ai_tensor
{
	enum ai_tensor_data_type type;
	union
	{
		void * 		data;
		float * 	f32;
		double * 	f64;
		uint8_t * 	u8;
		uint16_t * 	u16;
		uint32_t * 	u32;
		uint64_t *	u64;
	};
	int_dim4 dim[1];
	size_t length;
}ai_tensor_t;
ai_tensor_t * ai_tensor_init(ai_tensor_t * tensor, enum ai_tensor_data_type type, const int_dim4 * size, const void * data);
void ai_tensor_clear(ai_tensor_t * tensor);
int ai_tensor_resize(ai_tensor_t * tensor, const int_dim4 * new_size);

typedef struct ai_engine
{
	void * user_data;
	void * priv;			// ai_plugin_t *
	
	// virtual functions ( overided by plugins )
	int (* init)(struct ai_engine * engine, json_object * jconfig);
	void (* cleanup)(struct ai_engine * engine);
	
	int (* load_config)(struct ai_engine * engine, json_object * jconfig);
	int (* predict)(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults);
	int (* update)(struct ai_engine * engine, const ai_tensor_t * truth);
	int (* get_property)(struct ai_engine * engine, const char * name, void ** p_value);
	int (* set_property)(struct ai_engine * engine, const char * name, const void * value, size_t length);

	// public member functions
	ai_tensor_t * (* get_workspace)(struct ai_engine * engine);		// pre-allocated global memory (GPU or CPU)
}ai_engine_t;

ai_engine_t * ai_engine_init(ai_engine_t * engine, const char * plugin_type, void * user_data);
void ai_engine_cleanup(ai_engine_t * engine);

#ifdef __cplusplus
}
#endif
#endif
