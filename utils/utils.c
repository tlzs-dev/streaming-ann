/*
 * utils.c
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
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include "utils.h"

FILE * g_log_fp;

static const char s_hex_chars[] = "0123456789abcdef";
static const unsigned char s_hex_table[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,     8,  9, -1, -1, -1, -1, -1, -1,

	-1, 10, 11, 12, 13, 14, 15, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,

	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,

	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1,    -1, -1, -1, -1, -1, -1, -1, -1, 
};

						
static const char _b64[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
							'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
							'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
							'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
							'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
							'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
							'w', 'x', 'y', 'z', '0', '1', '2', '3',
							'4', '5', '6', '7', '8', '9', '+', '/'
							};
static const unsigned char _b64_digits[256] = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,    // + , /
	52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,    // 0-9
	-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
	15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,                   // A-Z
	-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
	41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

ssize_t bin2hex(const unsigned char * data, size_t length, char * hex)
{
	ssize_t	cb = (ssize_t)length * 2;
	for(ssize_t i = 0; i < length; ++i)
	{
		register unsigned char c = data[i];
		hex[i * 2 + 0] = s_hex_chars[((c >> 4) & 0x0F)];
		hex[i * 2 + 1] = s_hex_chars[((c) & 0x0F)];
	}
	hex[cb] = '\0';
	return cb;
}

ssize_t hex2bin(const char * hex, size_t length, unsigned char * data)
{
	ssize_t cb = length / 2;
	for(ssize_t i = 0; i < cb; ++i)
	{
		register unsigned char hi = s_hex_table[(unsigned char)hex[i * 2 + 0]];
		register unsigned char lo = s_hex_table[(unsigned char)hex[i * 2 + 1]];
		if(hi == -1 || lo == -1) return 0;
		
		data[i] = (hi << 4) | lo;
	}
	return cb;
}


/*************************************************
 * app_timer
*************************************************/
static app_timer_t g_timer[1];
double app_timer_start(app_timer_t * timer)
{
	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_MONOTONIC, ts);
	timer->begin = (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
	return timer->begin;
}
double app_timer_stop(app_timer_t * timer)
{
	struct timespec ts[1];
	memset(ts, 0, sizeof(ts));
	clock_gettime(CLOCK_MONOTONIC, ts);
	timer->end = (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
	return (timer->end - timer->begin);
}

void global_timer_start()
{
	app_timer_start(g_timer);
}

void global_timer_stop(const char * prefix)
{
	double time_elapsed = app_timer_stop(g_timer);
	if(NULL == prefix) prefix = "()";
	fprintf(stderr, "== [%s] ==: time_elapsed = %.6f ms\n", 
		prefix,
		time_elapsed * 1000.0);
	return;
}

ssize_t load_binary_data(const char * filename, unsigned char **p_dst)
{
	struct stat st[1];
	int rc;
	rc = stat(filename, st);
	if(rc)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::stat::%s\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			strerror(rc));
		return -1;
	}
	
	if(!S_ISREG(st->st_mode) )
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::not regular file!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename);
		return -1;
	}
	
	ssize_t size = st->st_size;
	if(size <= 0)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::%s(%s)::invalid file-length: %ld!\n", 
			__FILE__, __LINE__, __FUNCTION__, filename,
			(long)size
		);
		return -1;
	}
	if(NULL == p_dst) return (size + 1);		// return buffer size	( append '\0' for ptx file)
	
	FILE * fp = fopen(filename, "rb");
	assert(fp);
	
	unsigned char * data = *p_dst;
	*p_dst = realloc(data, size + 1);
	assert(*p_dst);
	
	data = *p_dst;
	ssize_t length = fread(data, 1, size, fp);
	fclose(fp);
	
	assert(length == size);	
	data[length] = '\0';
	return length;
}

#define is_white_char(c) 	((c) == ' ' || (c) == '\t' || (c) == '\r' || (c) == '\n')
char * trim_left(char * p_begin, char * p_end)
{
	assert(p_begin);
	if(NULL == p_end) p_end = p_begin + strlen(p_begin);
	while(p_begin < p_end && is_white_char(*p_begin)) ++p_begin;
	
	return p_begin;
}
char * trim_right(char * p_begin, char * p_end)
{
	assert(p_begin);
	if(NULL == p_end) p_end = p_begin + strlen(p_begin);
	while(p_end > p_begin && is_white_char(p_end[-1])) *--p_end = '\0';
	return p_begin;
}




int check_file(const char * path_name)
{
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc) return rc;
	
	if(S_ISREG(st->st_mode)) return 0;
	return 1;
}

int check_folder(const char * path_name, int auto_create)
{
	if(NULL == path_name || !path_name[0]) return -1;
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(path_name, st);
	if(rc) {
		if(auto_create)
		{
			char command[4096] = "";
			snprintf(command, sizeof(command), "mkdir -p %s", path_name);
			rc = system(command);
		}
		return rc;
	}
	
	if(S_ISDIR(st->st_mode)) return 0;
	return 1;
}




size_t base64_encode(const void * data, size_t data_len, char ** p_dst)
{
	if(NULL == data || data_len == 0) return 0;
	
	size_t cb = (data_len * 4 + 2) / 3;
	if(NULL == p_dst) return cb + 1;

	char * to = *p_dst;
	to = realloc(to, cb + 1); 
	assert(to);
	
	*p_dst = to;
	const unsigned char * p_data = (const unsigned char *)data;
	uint32_t * p = (uint32_t *)to;
	size_t i;
	
	size_t len = data_len / 3 * 3;
	size_t cb_left = data_len - len;
	
	for(i = 0; i < len; i += 3)
	{
		*p++ = MAKE_UINT32_LE( _b64[(p_data[i] >> 2) & 0x3F],
						_b64[((p_data[i] &0x03) << 4) | (((p_data[i + 1]) >> 4) & 0x0F)],
						_b64[((p_data[i + 1] & 0x0F) << 2) | ((p_data[i + 2] >> 6) & 0x03)],
						_b64[(p_data[i + 2]) & 0x3F]
						);
	}
	
	if(cb_left == 2)
	{
		*p++ = MAKE_UINT32_LE(_b64[(p_data[i] >> 2) & 0x3F],
							_b64[((p_data[i] &0x03) << 4) | (((p_data[i + 1]) >> 4) & 0x0F)],
							_b64[((p_data[i + 1] & 0x0F) << 2) | 0],
							'=');
		
	}else if(cb_left == 1)
	{
		*p++ = MAKE_UINT32_LE(_b64[(p_data[i] >> 2) & 0x3F],
							_b64[((p_data[i] &0x03) << 4) | 0],
							'=',
							'=');
	}
	
	cb = (char *)p - to;	
	to[cb] = '\0';
	
	return cb;
	
}


size_t base64_decode(const char * from, size_t cb_from, unsigned char ** p_dst)
{
	if(NULL == from) return 0;
	if(-1 == cb_from) cb_from = strlen(from);
	if(0 == cb_from) return 0;
	
	if(cb_from % 4) 
	{
		errno = EINVAL;
		return -1;
	}

	size_t dst_size = (cb_from / 4 * 3);
	
	if(NULL == p_dst) return dst_size;

	unsigned char * to = *p_dst;
	to = realloc(to, dst_size);
	assert(to);

	*p_dst = to;
	size_t count = cb_from / 4;
	
	const unsigned char * p_from = (const unsigned char *)from;
	const char * p_end = from + cb_from;
	
	unsigned char * p_to = to;
	union
	{
		uint32_t u;
		uint8_t c[4];
	}val;
	
	if(p_end[-1] == '=') count--;
	while(count--)
	{
		//~ val.u = *(uint32_t *)p_from;
		
		val.u = MAKE_UINT32_LE(	_b64_digits[p_from[0]], _b64_digits[p_from[1]],
								_b64_digits[p_from[2]], _b64_digits[p_from[3]]);
		
		if(val.c[0] == 0xff || val.c[1] == 0xff || val.c[2] == 0xff || val.c[3] == 0xff)
		{
			errno = EINVAL;
			return -1;
		}
		
		p_to[0] = (val.c[0] << 2) | ((val.c[1] >> 4) & 0x3);
		p_to[1] = ((val.c[1] & 0x0F) << 4) | ((val.c[2] >> 2) & 0x0F);
		p_to[2] = ((val.c[2] & 0x03) << 6) | (val.c[3] & 0x3F);
		
		p_from += 4;
		p_to += 3;
		
	}
	
	if(p_end[-1] == '=')
	{	
		if(p_end[-2] == '=')	
			val.u = MAKE_UINT32_LE(	_b64_digits[p_from[0]], _b64_digits[p_from[1]], 0, 0);
		else
			val.u = MAKE_UINT32_LE(	_b64_digits[p_from[0]], _b64_digits[p_from[1]], _b64_digits[p_from[2]], 0);
		
		if(val.c[0] == 0xff || val.c[1] == 0xff || val.c[2] == 0xff || val.c[3] == 0xff)
		{
			errno = EINVAL;
			return -1;
		}
		
		p_to[0] = (val.c[0] << 2) | ((val.c[1] >> 4) & 0x3);
		p_to[1] = ((val.c[1] & 0x0F) << 4) | ((val.c[2] >> 2) & 0x0F);
		p_to[2] = ((val.c[2] & 0x03) << 6) | (val.c[3] & 0x3F);
		p_to += 3;
	}
	return (size_t)(p_to - (unsigned char *)to);
}
