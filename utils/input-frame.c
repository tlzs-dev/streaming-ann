/*
 * input-frame.c
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

#include "img_proc.h"
#include "input-frame.h"
#include <json-c/json.h>


static const char * input_image_type_string[input_frame_type_png + 1] = {
	[input_frame_type_bgra] = "BGRA",
#ifndef _WIN32
	[input_frame_type_jpeg] = "image/jpeg",
	[input_frame_type_png] = "image/png",
#else
	[input_frame_type_jpeg] = ".jpg",
	[input_frame_type_png] = ".png",
#endif

};

#ifndef _WIN32
#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p)
	{
		free(p);
		*(void **)ptr = NULL;
	}
	return;
}
#else
#define AUTO_FREE_PTR
#endif

enum input_frame_type input_frame_type_from_string(const char * sz_type)
{
	enum input_frame_type type = input_frame_type_unknown;
	if(NULL == sz_type || !sz_type[0]) return type;

	static const char * delim = " +,;:\t";

	AUTO_FREE_PTR char * type_buf = strdup(sz_type);
	char * token = NULL;

	char * t = strtok_r(type_buf, delim, &token);
	int image_flags = 0;
	
	while(t)
	{
		if(strcasecmp(t, "image/jpeg") == 0 || strcasecmp(t, ".jpg") == 0)
		{
			if(image_flags) { type = input_frame_type_invalid; break; }
			image_flags = 1;
			type |= input_frame_type_jpeg;
		}else if(strcasecmp(t, "image/png") == 0 || strcasecmp(t, ".png") == 0)
		{
			if(image_flags) { type = input_frame_type_invalid; break; }
			image_flags = 1;
			type |= input_frame_type_png;
		}else if(strcasecmp(t, "bgra") == 0 || strcasecmp(t, "bgr") || strcasecmp(t, "grayscale") == 0)
		{
			if(image_flags) { type = input_frame_type_invalid; break; }
			image_flags = 1;
			type |= input_frame_type_bgra;
		}
		else if(strcasecmp(t, "json") == 0 || strcasecmp(t, "application/json") == 0 || strcasecmp(t, ".json") == 0)
		{
			type |= input_frame_type_json_flag;
		}
		t = strtok_r(NULL, delim, &token);
	}

#ifdef _WIN32
	free(type_buf);
	type_buf = NULL;
#endif
	return type;
}

ssize_t	input_frame_type_to_string(enum input_frame_type type, char sz_type[], size_t size)
{
	assert(sz_type);
	char * p = sz_type;
	char * p_end = p + size;
	int cb = 0;
	enum input_frame_type img_type = input_frame_type_unknown;
	if((img_type = (type & input_frame_type_image_masks)))
	{
		switch(img_type)
		{
		case input_frame_type_bgra:
		case input_frame_type_jpeg:
		case input_frame_type_png:
			cb = snprintf(p, p_end - p, "%s",  input_image_type_string[img_type]);
			break;
		default:
			snprintf(p, p_end - p, "%s", "unknown");
			cb = -1;
			break;
		}
		if(cb < 0) return 0;
		p += cb;
	}

	if(type & input_frame_type_json_flag)
	{
		cb = snprintf(p, p_end - p, "+%s", "application/json");
		assert(cb > 0);
	}

	return p_end - p;
}

void input_frame_clear(input_frame_t * frame)
{
	if(NULL == frame) return;
	if(frame->data)
	{
		free(frame->data);
		frame->data = NULL;
	}

	if(frame->json_str)
	{
		free(frame->json_str);
		frame->json_str = NULL;
	}
	memset(frame, 0, sizeof(*frame));
}



void input_frame_free(input_frame_t * frame)
{
	input_frame_clear(frame);
	free(frame);
	return;
}


input_frame_t * input_frame_new()
{
	input_frame_t * frame = calloc(1, sizeof(*frame));
	assert(frame);
	return frame;
}


int input_frame_set_json(input_frame_t * frame, const char * json_str, ssize_t cb_json)
{
	assert(frame);

	if(frame->json_str) {

		free(frame->json_str);
		frame->json_str = NULL;
		frame->cb_json = 0;
	}
	
	if(json_str)
	{
		if(cb_json <= 0) cb_json = strlen(json_str);

		if(cb_json > 0)
		{
			frame->type |= input_frame_type_json_flag;
			char * buf = calloc(1, cb_json + 1);
			assert(buf);

			memcpy(buf, json_str, cb_json);
			
			frame->json_str = buf;
			frame->cb_json = cb_json;
		}
	}
	return 0;
}

int input_frame_set_bgra(input_frame_t * frame, const bgra_image_t * bgra, const char * json_str, ssize_t cb_json)
{
	assert(frame);
	frame->type = input_frame_type_unknown;
	if(bgra)
	{
		frame->type |= input_frame_type_bgra;
		bgra_image_init(frame->bgra, bgra->width, bgra->height, bgra->data);
		frame->bgra->channels = bgra->channels;
		frame->bgra->stride = bgra->stride;

	}
	if(json_str) input_frame_set_json(frame, json_str, cb_json);
	return 0;
	
}

int input_frame_set_jpeg(input_frame_t * frame, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json)
{
	assert(frame);
	frame->type = input_frame_type_unknown;
	if(data)
	{
		int width = 0;
		int height = 0;

		int rc = img_utils_get_jpeg_size(data, length, &width, &height);

		if(0 == rc && width > 0 && height > 0)
		{
			frame->type |= input_frame_type_jpeg;
			unsigned char * buf = realloc(frame->data, length);
			assert(buf);
			memcpy(buf, data, length);
			
			frame->data = buf;
			frame->length = length;
			
			frame->width = width;
			frame->height = height;
		}else
		{
			return -1;	// invalid jpeg format
		}
	}
	if(json_str) input_frame_set_json(frame, json_str, cb_json);
	return 0;
}
int input_frame_set_png(input_frame_t * frame, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json)
{
	assert(frame);
	frame->type = input_frame_type_unknown;
	if(data)
	{
		int width = 0;
		int height = 0;

		int rc = img_utils_get_png_size(data, length, &width, &height);

		if(0 == rc && width > 0 && height > 0)
		{
			frame->type |= input_frame_type_png;
			unsigned char * buf = realloc(frame->data, length);
			assert(buf);
			memcpy(buf, data, length);
		
			frame->data = buf;
			frame->length = length;
			
			frame->width = width;
			frame->height = height;
		}else
		{
			return -1;	// invalid png format
		}
	}
	if(json_str) input_frame_set_json(frame, json_str, cb_json);
	return 0;
}


input_frame_t * input_frame_copy(input_frame_t * _dst, const input_frame_t * src)
{
	assert(src);
	input_frame_t * dst = _dst;
	if(NULL == dst)
	{
		dst = input_frame_new();
	}
	
	int image_type = src->type & input_frame_type_image_masks;
	int rc = -1;
	switch(image_type)
	{
	case input_frame_type_bgra:
		if(src->width <= 0 || src->height <= 0) break;
		rc = input_frame_set_bgra(dst, src->bgra, src->json_str, src->cb_json);
		break;
	case input_frame_type_png:
	case input_frame_type_jpeg:
		if(src->length <= 0) break;
		
		if(image_type == input_frame_type_png)
		{
			rc = input_frame_set_png(dst, src->data, src->length, src->json_str, src->cb_json);
		}else
		{
			rc = input_frame_set_jpeg(dst, src->data, src->length, src->json_str, src->cb_json);
		}
		break;
	default:
		break;
	}

	if(rc)
	{
		if(NULL == _dst)
		{
			free(dst);
			dst = NULL;
		}
	}else
	{
		dst->type = src->type;
		memcpy(dst->timestamp, src->timestamp, sizeof(dst->timestamp));
		dst->frame_number = src->frame_number;
	}
	
	return dst;
}
