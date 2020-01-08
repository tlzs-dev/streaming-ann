/*
 * ai-engine.c
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

#include "ai-engine.h"
#include "ann-plugin.h"


ai_tensor_t * ai_tensor_init(ai_tensor_t * tensor, enum ai_tensor_data_type type, const int_dim4 * size, const void * data)
{
	if(NULL == tensor) tensor = calloc(1, sizeof(*tensor));
	assert(tensor);

	tensor->type = type;
	int gpu_flags = type & ai_tensor_data_type_gpu_flags;
	type &= ai_tensor_data_type_masks;

	// currently only support float32
	assert(type == ai_tensor_data_type_float32); 
	size_t data_size = sizeof(float);

	size_t length = 0;
	if(size)
	{
		tensor->dim[0] = *size;
		length = size->n * size->c * size->h * size->w;
	}
	
	tensor->length = length;
	if(length > 0)
	{
		assert(!gpu_flags);
		tensor->data = realloc(tensor->data, length * data_size);
		assert(tensor->data);

		if(data)
		{
			memcpy(tensor->data, data, length * data_size);
		}
	}
	return tensor;
	
}
void ai_tensor_clear(ai_tensor_t * tensor)
{
	if(NULL == tensor) return;
	assert(0 == (tensor->type & ai_tensor_data_type_gpu_flags));	// TODO: ...

	free(tensor->data);
	memset(tensor, 0, sizeof(*tensor));
	return;
}

int ai_tensor_resize(ai_tensor_t * tensor, const int_dim4 * size)
{
	assert(tensor && size);
	assert(0 == (tensor->type & ai_tensor_data_type_gpu_flags));	// TODO: ...

	ssize_t length = size->n * size->c * size->h * size->w;
	
	size_t data_size = sizeof(float);	// TODO: ...

	if(length > tensor->length)
	{
		tensor->data = realloc(tensor->data, length * data_size);
		assert(tensor->data);
		
	}
	tensor->dim[0] = *size;
	tensor->length = length;
	return 0;
}

ai_engine_t * ai_engine_init(ai_engine_t * engine, const char * plugin_type, void * user_data)
{
	if(NULL == plugin_type) plugin_type = "ai-engine::darknet";
	
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_get_default();
	ann_plugin_t * plugin = helpler->find(helpler, plugin_type);
	if(NULL == plugin) {
		fprintf(stderr, "[ERROR]::%s()::unknown ai-engine type '%s'\n", __FUNCTION__, plugin_type);
		return NULL;
	}
	if(NULL == engine)
	{
		engine = calloc(1, sizeof(*engine));
		assert(engine);
	}

	engine->user_data = user_data;
	engine->init = plugin->init_func;
	
	return engine;
}

void ai_engine_cleanup(ai_engine_t * engine)
{
	if(engine && engine->cleanup)
	{
		engine->cleanup(engine);
	}
	return;
}
