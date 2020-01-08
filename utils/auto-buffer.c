/*
 * auto-buffer.c
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

#include "auto-buffer.h"

#define AUTO_BUFFER_ALLOCATION_SIZE	(65536)
auto_buffer_t * auto_buffer_init(auto_buffer_t * buf, ssize_t max_size)
{
	if(NULL == buf) buf = calloc(1, sizeof(*buf));
	assert(buf);

	int rc = auto_buffer_resize(buf, max_size);
	assert(0 == rc);
	return buf;
}

void auto_buffer_cleanup(auto_buffer_t * buf)
{
	if(buf->data)
	{
		free(buf->data);
	}
	memset(buf, 0, sizeof(*buf));
	return;
}

int auto_buffer_resize(auto_buffer_t * buf, ssize_t new_size)
{
	if(new_size <= buf->max_size) return 0;
	new_size = (new_size + AUTO_BUFFER_ALLOCATION_SIZE - 1) / AUTO_BUFFER_ALLOCATION_SIZE * AUTO_BUFFER_ALLOCATION_SIZE;
	assert(new_size > buf->max_size);

	buf->data = realloc(buf->data, new_size);
	assert(buf->data);
	buf->max_size = new_size;
	return 0;
}
ssize_t auto_buffer_push_data(auto_buffer_t * buf, const void * data, ssize_t length)
{
	if(NULL == data || length <= 0) return -1;
	assert(buf->length >= 0);
	
	int rc = auto_buffer_resize(buf, buf->length + length + 1);
	assert(0 == rc);

	memcpy(buf->data + buf->length, data, length);
	buf->length += length;
	buf->data[buf->length] = '\0';
	return (buf->length - buf->cur_pos);
}
ssize_t auto_buffer_peek_data(auto_buffer_t * buf, void * data, ssize_t length)
{
	ssize_t cb_avaliable = buf->length - buf->cur_pos;
	if(NULL == data) return (cb_avaliable + 1);
	
	if(length <= 0 || length > cb_avaliable) length = cb_avaliable;
	memcpy(data, buf->data + buf->cur_pos, length);
	return length;
}

ssize_t auto_buffer_pop_data(auto_buffer_t * buf, void * data, ssize_t length)
{
	if(data)
	{
		length = auto_buffer_peek_data(buf, data, length);
	}
	buf->cur_pos += length;
	
	assert(buf->cur_pos <= buf->length);
	if(buf->cur_pos == buf->length) auto_buffer_reset(buf);
	return length;
}

#undef AUTO_BUFFER_ALLOCATION_SIZE
