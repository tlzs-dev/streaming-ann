/*
 * httpd.c
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

#include <search.h>

#include "utils.h"
#include "httpd.h"

#include <sys/epoll.h>
#include <sys/types.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <pthread.h>
#include <netdb.h>

static pthread_mutex_t s_auto_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

#define auto_buffer_lock() 		pthread_mutex_lock(&s_auto_buf_mutex)
#define auto_buffer_unlock()	pthread_mutex_unlock(&s_auto_buf_mutex)

#define AUTO_LOCKER __attribute((cleanup(auto_unlock_mutex_ptr)))
static void auto_unlock_mutex_ptr(void * ptr)
{
	pthread_mutex_t * m = *(pthread_mutex_t **)ptr;
	if(m)
	{
		pthread_mutex_unlock(m);
	}
}

#define auto_locker() AUTO_LOCKER pthread_mutex_t * _m_ = &s_auto_buf_mutex; auto_buffer_lock();


/**
 * @ingroup output-module
 * @{
 */
//~ typedef struct auto_buffer
//~ {
	//~ unsigned char * data;
	//~ size_t size;
	//~ size_t length;
	//~ size_t cur_pos;

	//~ ssize_t (* push)(struct auto_buffer * buf, const void * data, size_t size);
	//~ ssize_t (* peek)(struct auto_buffer * buf, unsigned char ** p_dst, size_t size);

	//~ int (* resize)(struct auto_buffer * buf, size_t new_size);
	//~ int (* set_pos)(struct auto_buffer * buf, ssize_t new_pos);
//~ }auto_buffer_t;

//~ #define auto_buffer_reset(buf)  do { buf->length = 0; buf->cur_pos = 0; } while(0)
#define AUTO_BUFFER_ALLOC_SIZE	(65536)
static int http_auto_buffer_resize(http_auto_buffer_t * buf, size_t new_size)
{
	if(0 == new_size) new_size = AUTO_BUFFER_ALLOC_SIZE;
	assert(buf);
	if(new_size <= buf->size) return 0;
	new_size = (new_size + AUTO_BUFFER_ALLOC_SIZE - 1) / AUTO_BUFFER_ALLOC_SIZE * AUTO_BUFFER_ALLOC_SIZE;

	assert(new_size);
	buf->data = realloc(buf->data, new_size);
	assert(buf->data);

	memset(buf->data, 0, new_size - buf->size);
	buf->size = new_size;
	 
	return 0;
}

ssize_t http_auto_buffer_push_data(http_auto_buffer_t * buf, const void * data, size_t size)
{
	auto_locker();
	int rc = buf->resize(buf, buf->length + size);
	assert(0 == rc);

	memcpy(buf->data + buf->length, data, size);
	buf->length += size;
	
	return (ssize_t)size;
}

//~ ssize_t http_auto_buffer_pop_data (http_auto_buffer_t * buf, unsigned char ** p_dst, size_t dst_size);	==> peek and set_pos
ssize_t http_auto_buffer_peek_data (http_auto_buffer_t * buf, unsigned char ** p_dst, size_t dst_size)
{
	auto_locker();
	assert(buf);
	assert(buf->length >= buf->cur_pos);

	ssize_t cb = buf->length - buf->cur_pos;
	if(0 == cb) return 0;

	if(NULL == p_dst) return cb + 1;		// return buffer size
	assert(p_dst);
	unsigned char * dst = *p_dst;

	if(0 == dst_size || NULL == dst)	// auto allocate buffer
	{
		dst = realloc(dst, cb + 1);		// append tailing-zero to dst buffer
		assert(dst);

		*p_dst = dst;
	}else
	{
		if((size_t)cb > dst_size) cb = (ssize_t)dst_size;
	}
	memcpy(dst, buf->data + buf->cur_pos, cb);
	return cb;
}

int http_auto_buffer_set_pos(http_auto_buffer_t * buf, ssize_t new_pos)
{
	auto_locker();
	buf->cur_pos += new_pos;
	if(buf->cur_pos >= buf->length)
	{
		http_auto_buffer_reset(buf);
	}
	return 0;
}


http_auto_buffer_t * http_auto_buffer_init(http_auto_buffer_t * buf, size_t max_size)
{
	if(NULL == buf) buf = calloc(1, sizeof(*buf));
	assert(buf);

	buf->push = http_auto_buffer_push_data;
	buf->peek = http_auto_buffer_peek_data;
	buf->set_pos = http_auto_buffer_set_pos;
	buf->resize = http_auto_buffer_resize;
	
	int rc = http_auto_buffer_resize(buf, 0);
	assert(0 == rc);
	return buf;
}

void http_auto_buffer_cleanup(http_auto_buffer_t * buf)
{
	if(NULL == buf) return;
	free(buf->data);
	memset(buf, 0, sizeof(*buf));
	return;
}

/**
 * @}
 */

/**
 * @ingroup output-module
 * @{
 */
//~ typedef struct http_header_field
//~ {
	//~ char * key;
	//~ char * value;
//~ }http_header_field_t;

int http_header_field_compare(const http_header_field_t * a, const http_header_field_t * b)	// for tsearch
{
	assert(a || b);
	if(NULL == a) return -1;
	if(NULL == b) return 1;
	return strcasecmp(a->key, b->key);
}

int http_header_field_ptr_compare(const void * _a, const void * _b)		// for qsort
{
	const http_header_field_t * a = *(const http_header_field_t **)_a;
	const http_header_field_t * b = *(const http_header_field_t **)_b;
	assert(a || b);
	if(NULL == a) return -1;
	if(NULL == b) return 1;
	return strcasecmp(a->key, b->key);
}

int http_header_field_set(http_header_field_t * field, const char * key, const char * value)
{
	assert(key);
	if(field->key) free(field->key);
	field->key = strdup(key);

	if(field->value)
	{
		free(field->value);
		field->value = NULL;
	}

	if(value) field->value = strdup(value);
	return 0;
}

void http_header_field_cleanup(http_header_field_t * field)
{
	if(NULL == field) return;

	if(field->key) 		free(field->key);
	if(field->value) 	free(field->value);
	field->key = NULL;
	field->value = NULL;
	return;
}


//~ typedef struct http_header
//~ {
	//~ long response_code;
	//~ enum http_stage stage;		// current parsed stage
	
	//~ http_header_field_t ** fields;
	//~ size_t max_fields;
	//~ size_t fields_count;
	
	//~ const char * method;
	//~ const char * path;
	//~ const char * protocol;
	//~ const char * query_string;
	
	//~ int (* set)(struct http_header * hdr, const char * key, const char * value);
	//~ const char * (* get)(struct http_header * hdr, const char * key);
	//~ int (* remove)(struct http_header * hdr, const char * key);
	//~ int (* clear)(struct http_header * hdr);
	
	//~ int (* sort)(struct http_header * hdr);
	//~ const char * (* to_string)(struct http_header * hdr);
	
	//~ /* private data */
	//~ http_auto_buffer_t buffer[1];
	//~ void * fields_root;		// tsearch root
//~ }http_header_t;

static int 			http_header_set(struct http_header * hdr, const char * key, const char * value)
{
	http_header_field_t * field = calloc(1, sizeof(*field));
	assert(field);
	int rc = http_header_field_set(field, key, value);
	assert(0 == rc);

	http_header_field_t ** p_field = tsearch(field, &hdr->fields_root,
		(int (*)(const void *, const void *))http_header_field_compare);
	assert(p_field);

	if(*p_field == field)	// new header item
	{
		hdr->fields[hdr->fields_count] = field;
		++hdr->fields_count;
		assert(hdr->fields_count < hdr->max_fields);
	}else
	{
		(*p_field)->value = field->value;

		field->value = NULL;
		http_header_field_cleanup(field);
		free(field);
	}
	return 0;
}
static const char * 	http_header_get(struct http_header * hdr, const char * key)
{
	http_header_field_t pattern[1] = {{
		.key = (char *)key,
	}};
	http_header_field_t ** p_field = tfind(pattern, &hdr->fields_root,
		(int (*)(const void *, const void *))http_header_field_compare);
	if(p_field && *p_field)
	{
		return (*p_field)->value;
	}
	return NULL;
}
static int 			http_header_remove(struct http_header * hdr, const char * key)
{
	http_header_field_t pattern[1] = {{
		.key = (char *)key,
	}};
	http_header_field_t ** p_field = tfind(pattern, &hdr->fields_root, (int (*)(const void *, const void *))http_header_field_compare);
	if(NULL == p_field || NULL == (*p_field)) return -1;

	http_header_field_t * item = * p_field;
	tdelete(pattern, &hdr->fields_root, (int (*)(const void *, const void *))http_header_field_compare);

	http_header_field_cleanup(item);
	free(item);

	http_header_field_t ** fields = hdr->fields;
	for(size_t i = 0; i < hdr->fields_count; ++i)
	{
		if(fields[i] == item)
		{
			--hdr->fields_count;
			if(i == hdr->fields_count)
			{
				fields[i] = NULL;
			}else
			{
				fields[i] = fields[hdr->fields_count];
				fields[hdr->fields_count] = NULL;
			}
			break;
		}
	}
	return 0;
}
static int 			http_header_clear(struct http_header * hdr)
{
	if(NULL == hdr) return -1;
	if(hdr->fields_root)
	{
		tdestroy(hdr->fields_root, (void (*)(void *))http_header_field_cleanup);
		hdr->fields_root = NULL;
	}
	
	for(int i = 0; i < hdr->fields_count; ++i)
	{
		http_header_field_t * field = hdr->fields[i];
		assert(field);
		free(field);
		hdr->fields[i] = NULL;
	}
	free(hdr->fields);
	hdr->fields = NULL;
	hdr->fields_count = 0;
	return 0;
}

static int 			http_header_sort(struct http_header * hdr)
{
	qsort(hdr->fields, hdr->fields_count, sizeof(hdr->fields[0]), http_header_field_ptr_compare);
	return 0;
}

static const char * 	http_header_to_string(struct http_header * hdr, int sort)
{
	http_auto_buffer_t * buf = hdr->buffer;		// priv buffer
	buf->resize(buf, 0);
	assert(buf->data);

	if(sort) hdr->sort(hdr);
	
	buf->data[0] = '\0';
	http_header_field_t ** fields = hdr->fields;
	for(int i = 0; i < hdr->fields_count; ++i)
	{
		char line[4096] = "";
		http_header_field_t * field = fields[i];
		assert(field && field->key);

		const char * value = field->value;
		if(NULL == value) value = "";			// avoid nullptr
		int cb = snprintf(line, sizeof(line), "%s:%s\r\n", field->key, field->value);
		buf->push(buf, line, cb);
	}
	buf->push(buf, "\r\n", 3);	// with tailing '\0'

	http_auto_buffer_reset(buf);
	return (const char *)buf->data;
}

static inline int http_hdr_is_eol(const char * line)		// http header end of line
{
	return ( (line[0] == '\n') || (line[0] == '\r' && line[1] == '\n') );
}

enum http_stage http_header_parse(http_header_t * hdr, http_auto_buffer_t * buf)
{
	char * buffer = NULL;
	ssize_t cb_buffer = 0;

	enum http_stage stage = hdr->stage;
	assert(stage < http_stage_read_data);
	
	cb_buffer = http_auto_buffer_peek_data(buf, (unsigned char **)&buffer, 0);
	if(cb_buffer < 0) return http_stage_unknown;
	if(cb_buffer == 0) return http_stage_need_data;

	char * p = buffer;
	char * p_end = buffer + cb_buffer;

	char * line = p;
	char * next_line = NULL;

	while(line && line < p_end)
	{
		next_line = strchr(line, '\n');
		if(NULL == next_line) {
			stage = http_stage_need_data;
			break;
		}

		*next_line++ = '\0';
		

		if(hdr->stage == http_stage_init)
		{
			// parse request method and protcol
			char * tok = NULL;
			char * path, * query_string, *protocol;
			char * method = strtok_r(line, " ", &tok);

			if(NULL == method)
			{
				stage = http_stage_unknown;	// error
				break;
			}
			assert(method);
			path = strtok_r(NULL, " ", &tok);
			if(NULL == path)
			{
				stage = http_stage_unknown;	// error
				break;
			}
			
			query_string = strchr(path, '?');
			if(query_string) *query_string++ = '\0';
			protocol = strtok_r(NULL, " ", &tok);
			if(NULL == protocol)
			{
				stage = http_stage_unknown;	// error
				break;
			}

			hdr->method = strdup(method);
			hdr->path = strdup(path);
			if(query_string) hdr->query_string = strdup(query_string);
			hdr->protocol = strdup(protocol);

			
			stage = hdr->stage = http_stage_parse_header;
		}else
		{
			log_printf("parse line: %s\n...", line);
			assert(hdr->stage == http_stage_parse_header);
			char * key = line;
			char * value = strchr(key, ':');
			
			if(key) trim(key, value);
			assert(key && key[0]);
			
			if(value)
			{
				*value++ = '\0';
				trim(value, NULL);
			}
			hdr->set(hdr, key, value);
		}
		
		//...
		line = next_line;
		if(http_hdr_is_eol(next_line))
		{
			stage = hdr->stage = http_stage_read_data;		// all headers been read
			line = next_line;
			line += (next_line[0]=='\n')?1:2;
			break;
		}
		
	}

	ssize_t cb = line - buffer;
	buf->set_pos(buf, buf->cur_pos + cb);		// move to the end of parsed data

	free(buffer);
	return stage;
}

http_header_t * http_header_init(http_header_t * hdr, size_t max_fields)
{
	log_printf("");
	if(NULL == hdr) hdr = calloc(1, sizeof(*hdr));
	assert(hdr);

	http_auto_buffer_init(hdr->buffer, 0);

	hdr->set = http_header_set;
	hdr->get = http_header_get;
	hdr->remove = http_header_remove;
	hdr->clear = http_header_clear;

	hdr->sort = http_header_sort;
	hdr->to_string = http_header_to_string;		// serialize
	hdr->parse = http_header_parse;

#define MAX_FIELDS	(4096)
	if(0 == max_fields) max_fields = MAX_FIELDS;

	hdr->fields = calloc(max_fields, sizeof(*hdr->fields));
	assert(hdr->fields);
	hdr->max_fields = max_fields;

#undef MAX_FIELDS
	return hdr;
}

void http_header_cleanup(http_header_t * hdr)
{
	if(NULL == hdr) return;
	hdr->clear(hdr);

	http_auto_buffer_cleanup(hdr->buffer);
	free(hdr->method);
	free(hdr->path);
	free(hdr->query_string);
	free(hdr->protocol);
	
	memset(hdr, 0, sizeof(*hdr));
	return;
}




/**
 * @ingroup http_session		peer_info
 * @{
 */
//~ typedef struct http_session
//~ {
	//~ struct http_server * server;
	//~ void * user_data;
	
	//~ int fd;		// client_fd
	//~ int quit;
	
	//~ int async_mode;		// 0: event (edge_trigger) mode ; 1: thread mode
	//~ pthread_t th;
	//~ pthread_mutex_t mutex;
	//~ pthread_cond_t cond;
	
	//~ enum http_stage stage;
	//~ http_header_t request_hdr[1];
	//~ http_header_t response_hdr[1];
	
	//~ http_auto_buffer_t in_buf[1];
	//~ http_auto_buffer_t out_buf[1];
	
	//~ enum http_stage (* on_read)(struct http_session * session, void * user_data);
	//~ enum http_stage (* on_write)(struct http_session * session, void * user_data);

	//~ int (* on_error)(struct http_session * session, void * user_data);
//~ }http_session_t;

static ssize_t read_avialable_data(int fd, http_auto_buffer_t * in_buf)
{
	log_printf("");
	ssize_t	cb = 0;
	ssize_t total_bytes = 0;

	char buf[4096] = "";
	while(1)
	{
		cb = read(fd, buf, sizeof(buf) - 1);
		if(cb == 0)	// remote closed connection
		{
			return -1;
		}
		if(cb < 0)
		{
			if(errno == EWOULDBLOCK) break;
			return -1;
		}
		in_buf->push(in_buf, buf, cb);
		total_bytes += cb;
	}

	return total_bytes;
}


static enum http_stage http_session_on_read(struct http_session * session, void * user_data)
{
	http_header_t * hdr = session->request_hdr;
	enum http_stage stage = hdr->stage;
	log_printf("stage: %d", (int)stage);
	
	http_auto_buffer_t * in_buf = session->in_buf;
	ssize_t cb = 0;
	if(hdr->stage < http_stage_read_data)
	{
		cb = read_avialable_data(session->fd, in_buf);
		if(cb < 0) {
			session->on_error(session, user_data);
			return http_stage_cleanup;
		}
		stage = hdr->parse(hdr, in_buf);
	}

	if(stage >= http_stage_request_final)
	{
		session->on_error(session, user_data);
		return http_stage_cleanup;
	}
	
	if(stage == http_stage_read_data)
	{
		long content_length = hdr->content_length;
		cb = read_avialable_data(session->fd, in_buf);
		if(cb < 0) // connection closed
		{
			session->on_error(session, user_data);
			return http_stage_cleanup;
		}
		
		if((in_buf->length - in_buf->cur_pos) >= content_length)
		{
			if(session->on_request) {
				stage = session->on_request(session);
			}
			else
			{
				stage = http_stage_request_final;
			}
		}
	}
	return stage;
}

static enum http_stage http_session_on_write(struct http_session * session, void * user_data)
{
	pthread_mutex_lock(&session->mutex);
	
	http_auto_buffer_t * buf = session->out_buf;

	
	ssize_t cb = 0;
	ssize_t cb_avaliable = buf->length - buf->cur_pos;
	if(cb_avaliable > 0)
	{
		cb = write(session->fd, buf->data, cb_avaliable);
		if(cb <= 0)		// connection closed
		{
			int err = errno;
			if(cb == 0 || err != EWOULDBLOCK)
			{
				session->on_error(session, user_data);
				pthread_mutex_unlock(&session->mutex);
				return http_stage_cleanup;
			}
		}else
		{
			cb_avaliable -= cb;
			buf->set_pos(buf, buf->cur_pos + cb);
		}
	}
	pthread_mutex_unlock(&session->mutex);
	if(cb_avaliable == 0)	// all data been written
	{
		tcp_server_t * tcp = (tcp_server_t *)session->server;
		assert(tcp && tcp->efd > 0);
	
		int efd = tcp->efd;
		struct epoll_event ev[1];
		memset(ev, 0, sizeof(ev));
		ev->data.ptr = session;
		ev->events = EPOLLIN | EPOLLET;			// disable write-end events
		int rc = epoll_ctl(efd, EPOLL_CTL_MOD, session->fd, ev);
		assert(0 == rc);
	}
	return http_stage_cleanup;
}

static int http_session_on_error(struct http_session * session, void * user_data)
{
	tcp_server_t * tcp = (tcp_server_t *)session->server;
	assert(tcp && tcp->efd > 0);

	int efd = tcp->efd;
	int rc = epoll_ctl(efd, EPOLL_CTL_DEL, session->fd, NULL);

	UNUSED(rc);
	http_session_free(session);
	return 0;
}

static int http_session_on_request(struct http_session * session)
{
	// parse request
	// message_handler(...)

	http_auto_buffer_t * in_buf = session->in_buf;
	const char * req_header = session->request_hdr->to_string(session->request_hdr, 1);
	
	printf("%s()::hdr: %s\nin_buf: cur_pos: %ld, length: %ld, post_data: %s\n"
		"cur_stage: %d\n",
		__FUNCTION__,
		req_header,
		(long)in_buf->cur_pos, (long)in_buf->length,
		(char *)in_buf->data + in_buf->cur_pos,
		(int)session->request_hdr->stage
		);

	session->request_hdr->stage = http_stage_request_final;
	session->on_response(session);
	return 0;
}

static int http_session_on_response(struct http_session * session)
{
	//~ http_auto_buffer_t * buf = session->out_buf;
	//~ static const char response[] = "HTTP/1.1 200 OK\r\n"
		//~ "Content-Type: text/html\r\n"
		//~ "Content-Length: 11\r\n"
		//~ "\r\n"
		//~ "0123456789\n";
	//~ buf->push(buf, response, sizeof(response) - 1);

	log_printf("out_buf: length=%ld", (long)session->out_buf->length);
	tcp_server_t * tcp = (tcp_server_t *)session->server;
	assert(tcp && tcp->efd > 0);

	int efd = tcp->efd;
	struct epoll_event ev[1];
	memset(ev, 0, sizeof(ev));
	ev->data.ptr = session;
	ev->events = EPOLLIN | EPOLLET | EPOLLOUT;					// add write-end events
	int rc = epoll_ctl(efd, EPOLL_CTL_MOD, session->fd, ev);
	assert(0 == rc);

	session->request_hdr->stage = http_stage_response_final;
	
	return 0;
}



http_session_t * http_session_new(struct tcp_server * server, int peer_fd, int async_mode, void * user_data)
{
	http_session_t * session = calloc(1, sizeof(*session));
	assert(session);

	int rc = pthread_mutex_init(&session->mutex, NULL);
	assert(0 == rc);
	rc = pthread_cond_init(&session->cond, NULL);
	assert(0 == rc);

	http_server_t * http = (http_server_t *)server;
	session->server = http;
	session->fd = peer_fd;
	session->async_mode = async_mode;
	session->user_data = user_data;

	http_header_init(session->request_hdr, 0);
	http_header_init(session->response_hdr, 0);
	session->response_hdr->response_code = 404;		// default: return 404 Not Found

	http_auto_buffer_init(session->in_buf, 0);			// request data	buffer
	http_auto_buffer_init(session->out_buf, 0);			// response data buffer

	// socket events
	session->on_read = http_session_on_read;
	session->on_write = http_session_on_write;
	session->on_error = http_session_on_error;

	// default callback
	session->on_request = http->on_request?http->on_request:http_session_on_request;
	session->on_response = http->on_response?http->on_response:http_session_on_response;

	if(async_mode)
	{
		// TODO: ...
	}

	return session;
}

void http_session_free(http_session_t * session)
{
	if(NULL == session) return;

	pthread_mutex_lock(&session->mutex);
	int fd = session->fd;
	session->fd = -1;
	http_server_t * server = session->server;
	if(server)
	{
		server->remove_session(server, session);
	}

	if(fd > 0)
	{
		tcp_server_t * tcp = (tcp_server_t *)server;
		if(tcp->efd > 0)
		{
			epoll_ctl(tcp->efd, EPOLL_CTL_DEL, fd, NULL);
		}
		close(fd);
		fd = -1;
	}

	pthread_mutex_unlock(&session->mutex);

	pthread_mutex_destroy(&session->mutex);
	pthread_cond_destroy(&session->cond);

	http_auto_buffer_cleanup(session->in_buf);
	http_auto_buffer_cleanup(session->out_buf);

	http_header_cleanup(session->request_hdr);
	http_header_cleanup(session->response_hdr);

	free(session);
	return;
}
/**
 * @}
 */




/**
 * @ingroup tcp_server
 * @{
 */
//~ typedef struct tcp_server
//~ {
	//~ int fd;		// server_fd
	//~ int efd;	// epoll fd

	//~ void * user_data;
	//~ void * priv;
	//~ int (* on_accept)(struct tcp_server * sock);
	//~ int (* on_error)(struct tcp_server * sock);
//~ }tcp_server_t;

static int tcp_server_on_accept(struct tcp_server * tcp, void * user_data)
{
	return 0;
}
static int tcp_server_on_error(struct tcp_server * tcp, void * user_data)
{
	return 0;
}

tcp_server_t * tcp_server_init(tcp_server_t * tcp, void * user_data)
{
	if(NULL == tcp) tcp = calloc(1, sizeof(*tcp));
	assert(tcp);

	tcp->user_data = user_data;
	tcp->on_accept = tcp_server_on_accept;		// default process, overridable
	tcp->on_error = tcp_server_on_error;		

	return tcp;
}

void tcp_server_cleanup(tcp_server_t * tcp)
{
	if(NULL == tcp) return;
	if(tcp->fd > 0)
	{
		close(tcp->fd);
		tcp->fd = -1;
	}
	if(tcp->efd > 0)
	{
		close(tcp->efd);
		tcp->efd = 0;
	}
	return;
}


/**
 * @ingroup http_server
 * @{
 */
//~ typedef struct http_server
//~ {
	//~ tcp_server_t base[1];
	//~ http_session_t ** sessions;
	//~ size_t max_session;
	//~ size_t sessions_count;
	
	//~ void * session_root;	// tsearch root
	//~ int quit;
	
	//~ int local_only;			// only accept connections from localhost or via ssh-tunnel, (no need to use SSL) 
	//~ int use_ssl;
	//~ char * ca_file;			// if use private CA 
	//~ char * key_file;		// privkey / cert stored seperately
	//~ char * cert_file;		// server cert.
	
	//~ int async_mode;
	//~ pthread_t th;
	//~ pthread_mutex_t mutex;
	
	//~ int (* run)(struct http_server * http, int async_mode);		// async_mode: Server Runing Mode. 0 == blocked; 1 == thread mode
	//~ int (* stop)(struct http_server * http);
	
//~ }http_server_t;

static int http_server_on_accept(struct tcp_server * tcp, void * user_data)
{
	http_server_t * http = (http_server_t *)tcp;
	assert(http);

	struct sockaddr_storage ss[1];
	memset(ss, 0, sizeof(ss));
	socklen_t len = sizeof(ss);
	int client_fd = accept(tcp->fd, (struct sockaddr *)ss, &len);
	if(client_fd > 0)
	{

		char hbuf[NI_MAXHOST] = "";
		char sbuf[NI_MAXSERV] = "";
		int rc = getnameinfo((struct sockaddr *)ss, len,
			hbuf, sizeof(hbuf),
			sbuf, sizeof(sbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
		assert(0 == rc);
		fprintf(stderr, "[INFO]::%s()::new connection on %s:%s\n",
			__FUNCTION__,
			hbuf, sbuf);
			
		make_nonblock(client_fd);
		int async_mode = 0;		// default: 0, event-mode;   (1==thread mode; for mjpeg-streaming)
		http_session_t * session = http_session_new(tcp, client_fd,
			async_mode, NULL);
		assert(session);

		struct epoll_event ev[1];
		memset(ev, 0, sizeof(ev));
		ev->data.ptr = session;
		ev->events = EPOLLIN | EPOLLET;		// edge trigger mode
		rc = epoll_ctl(tcp->efd, EPOLL_CTL_ADD, client_fd, ev);
		assert(0 == rc);

		http->add_session(http, session);
		
		return 0;
	}
	return -1;
}
static int http_server_on_error(struct tcp_server * tcp, void * user_data)
{
	http_server_t * http = (http_server_t *)tcp;
	assert(http);


	http_server_cleanup(http);
	return 0;
}

static int http_session_compare(const void * a, const void * b)
{
	if( ((unsigned long)a) > ((unsigned long)b) ) return 1;
	if( ((unsigned long)a) < ((unsigned long)b) ) return -1;
	return 0;
}

static int http_server_add_session(struct http_server * http, 		http_session_t * session)
{
#define MAX_SESSIONS	(65536)
	http_session_t ** sessions = http->sessions;
	if(http->sessions_count >= http->max_sessions)
	{
		size_t new_size = http->max_sessions + MAX_SESSIONS;
		sessions = realloc(sessions, new_size * sizeof(*sessions));
		assert(sessions);

		memset(sessions + http->max_sessions, 0, (new_size - http->max_sessions) * sizeof(*sessions));
		http->sessions = sessions;
		http->max_sessions = new_size;
	}

	http->sessions[http->sessions_count++] = session;
	tsearch(session, &http->sessions_root, http_session_compare);

#undef MAX_SESSIONS	
	return 0;
}

static int http_server_remove_session(struct http_server * http, 	http_session_t * session)
{
	if(NULL == http || NULL == http->sessions) return -1;
	
	http_session_t ** sessions = tfind(session, &http->sessions_root, http_session_compare);
	if(NULL == sessions || NULL == *sessions) return -1;

	tdelete(session, &http->sessions_root, http_session_compare);

	sessions = http->sessions;
	for(size_t i = 0; i < http->sessions_count; ++i)
	{
		if(sessions[i] == session)
		{
			--http->sessions_count;
			if(i == (http->sessions_count - 1))
			{
				if(session->fd > 0)
				{
					close(session->fd);
				}
				sessions[i] = NULL;
			}else
			{
				sessions[i] = sessions[http->sessions_count];
				sessions[http->sessions_count] = NULL;
			}
		}
	}
	return 0;
}
	


static void * http_server_thread(void * user_data)
{
	int rc = 0;
	http_server_t * http = user_data;
	tcp_server_t * tcp = user_data;
	assert(http);

	int server_fd = tcp->fd;
	int efd = tcp->efd;

	int timeout = 1000;
	sigset_t sigs[1];
	sigemptyset(sigs);
	sigaddset(sigs, SIGPIPE);
	sigaddset(sigs, SIGALRM);
	sigaddset(sigs, SIGCHLD);
	sigaddset(sigs, SIGCONT);
	sigaddset(sigs, SIGINT);
	sigaddset(sigs, SIGUSR1);
	
#define MAX_EVENTS 	(64)

	while(!http->quit)
	{
		struct epoll_event events[MAX_EVENTS];
		memset(events, 0, sizeof(events));
		
		int n = epoll_pwait(efd, events, MAX_EVENTS, timeout, sigs);
		if(0 == n) continue;
		if(n < 0)
		{
			int err = errno;
			fprintf(stderr, "[ERROR]::%s()::unknown server error or unblocked signals, errno=%d, errmsg=%s\n",
				__FUNCTION__,
				err,
				strerror(err)
				);
			continue;
		//	http->quit = 1;
			break;
		}

		for(int i = 0; i < n; ++i)
		{
			struct epoll_event * ev = &events[i];

			/* server events */
			if(ev->data.fd == server_fd)
			{
				if(ev->events & EPOLLIN)		// new connection
				{
					tcp->on_accept(tcp, http);
				}else
				{
					tcp->on_error(tcp, http);
					break;
				}
				continue;
			}

			http_session_t * session = ev->data.ptr;
			assert(session);

			if(ev->events & EPOLLIN || ev->events & EPOLLOUT)
			{
				if(ev->events & EPOLLIN)		session->on_read(session, http);
				if(ev->events & EPOLLOUT)		session->on_write(session, http);
			}else
			{
				fprintf(stderr, "[ERROR]::%s()::unknown client(%p) error\n", __FUNCTION__, session);
				
				http->remove_session(http, session);
				session->on_error(session, http);
				free(session);
			}
		}
	}

	fprintf(stderr, "\e[31m[ERROR]::%s()::http server exited\e[39m\n", __FUNCTION__);
	exit(1);
	
	if(http->async_mode) pthread_exit((void *)(long)rc);
#undef MAX_EVENTS
	return (void *)(long)rc;
}

static int http_server_run(struct http_server * http, int async_mode)		// async_mode: Server Runing Mode. 0 == blocked; 1 == thread mode
{
	http->async_mode = async_mode;
	int rc = 0;
	if(async_mode)
	{
		rc = pthread_create(&http->th, NULL, http_server_thread, http);
	}else
	{
		rc = (int)(long)http_server_thread(http);
	}
	return rc;
}

static int http_server_stop(struct http_server * http)
{
	http->quit = 1;
	return 0;
}

http_server_t * http_server_init(http_server_t * http, const char * host_name, const char * port, int local_only)
{
	int rc = 0;
	struct addrinfo hints, *serv_info = NULL, *p;
	if(local_only) host_name = "localhost";

	if(NULL == port) port = "8088";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	
	rc = getaddrinfo(host_name, port, &hints, &serv_info);
	if(rc)
	{
		fprintf(stderr, "[ERROR]::%s()::getaddrinfo(%s:%s) failed: %s\n",
			__FUNCTION__,
			host_name, port,
			gai_strerror(rc));
		return NULL;
	}

	int server_fd = -1;

	for(p = serv_info; NULL != p; p = p->ai_next)
	{
		server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(server_fd < 0) continue;

		rc = setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
		assert(0 == rc);
		
		rc = bind(server_fd, (struct sockaddr *)p->ai_addr, p->ai_addrlen);

		char hbuf[NI_MAXHOST] = "";
		char sbuf[NI_MAXSERV] = "";
		rc = getnameinfo(p->ai_addr, p->ai_addrlen,
			hbuf, sizeof(hbuf),
			sbuf, sizeof(sbuf),
			NI_NUMERICHOST | NI_NUMERICSERV);
		assert(0 == rc);
		
		if(rc)
		{
			int err = errno;
			fprintf(stderr, "[WARNING]::%s()::bind(%s:%s) failed: %s\n", __FUNCTION__,
				hbuf, sbuf, strerror(err));
			close(server_fd);
			server_fd = -1;
			continue;
		}

		rc = listen(server_fd, 16);
		if(rc)
		{
			int err = errno;
			fprintf(stderr, "[WARNING]::%s()::listen(%s:%s) failed: %s\n", __FUNCTION__,
				hbuf, sbuf, strerror(err));
			close(server_fd);
			server_fd = -1;
			continue;
		}

		fprintf(stderr, "[INFO]::listenint on %s:%s\n", hbuf, sbuf);
		make_nonblock(server_fd);
		break;
	}

	assert(p);

	memcpy(&hints, p, sizeof(hints));
	freeaddrinfo(serv_info);
	
	if(NULL == http) http = calloc(1, sizeof(*http));
	assert(http);

	tcp_server_t * tcp = tcp_server_init(http->base, http);
	tcp->on_accept = http_server_on_accept;
	tcp->on_error = http_server_on_error;
	
	http->run = http_server_run;
	http->stop = http_server_stop;

	http->add_session = http_server_add_session;
	http->remove_session = http_server_remove_session;

	int efd = epoll_create1(0);
	assert(efd > 0);

	struct epoll_event ev[1];
	memset(ev, 0, sizeof(ev));
	ev->data.fd = server_fd;
	ev->events = EPOLLIN | EPOLLHUP | EPOLLRDHUP;

	rc = epoll_ctl(efd, EPOLL_CTL_ADD, server_fd, ev);
	assert(0 == rc);

	tcp->fd = server_fd;
	tcp->efd = efd;

	return http;
}

void http_server_cleanup(http_server_t * http)
{
	if(http->th)
	{
		http->quit = 1;
		void * exit_code = NULL;
		int rc = pthread_join(http->th, &exit_code);
		fprintf(stderr, "[MSG]::%s()::pthread_join exited with code %ld, rc = %d\n",
			__FUNCTION__,
			(long)exit_code,
			rc);
	}

	// TODO: clear sessions
	// ...

	// close socket 
	tcp_server_cleanup((tcp_server_t *)http);
	return;
}



//////////////////////////////////////////////////////////////////
// TEST Module

#if defined(_TEST_HTTPD) && defined(_STAND_ALONE)

void wait_key(void)
{
	char buf[100] = "";
	char * line = NULL;

	while((line = fgets(buf, sizeof(buf) - 1, stdin)))
	{
		if(line[0] == 'q') break;
	}
	return;
}

static http_server_t g_http[1];
void on_signal(int sig)
{
	switch(sig)
	{
	case SIGINT: case SIGUSR1:
		g_http->quit = 1;
		close(0);	// close stdin
		break;
	default:
		exit((128 + sig));
	}
	return;
}


int main(int argc, char **argv)
{
	signal(SIGINT, on_signal);
	signal(SIGUSR1, on_signal);
	
	/* test http header */

	
	
	http_header_t hdr[1];
	memset(hdr, 0, sizeof(hdr));

	static const char * request_hdr =
		"GET /path?querystring  HTTP/1.1\r\n"
		"Content-Type: text/html;charset=utf-8\r\n"
		"Content-Length: 0\r\n"
		"\r\n"
		"ja;sjdfj;ajdf;"
		;

	if(argc > 1) request_hdr = argv[1];

	http_header_init(hdr, 0);

	http_auto_buffer_t buf[1];
	memset(buf, 0, sizeof(buf));
	http_auto_buffer_init(buf, 0);

	buf->push(buf, request_hdr, strlen(request_hdr));

	log_printf("request_buf: length=%Zu\n", buf->length);
	enum http_stage stage = http_stage_unknown;
	
	stage = hdr->parse(hdr, buf);
	log_printf("stage: %d, buf->cur_pos: %Zu, unparsed_data: %s\n",
		stage,
		buf->cur_pos,
		(char *)&buf->data[buf->cur_pos]);

	log_printf("========\nrequest: %s\n=========\n", request_hdr);

	const char * response_hdr =  hdr->to_string(hdr, 1);
	log_printf("respoonse_hdr: %s",
		response_hdr);

	http_auto_buffer_cleanup(buf);
	http_header_cleanup(hdr);

	/* test http server */
	http_server_t * http = http_server_init(g_http, NULL, "8088", 0);

	if(NULL == http)
	{
		exit(111);
	}

	http->run(http, 1);
	
	wait_key();

	http->stop(http);
	http_server_cleanup(http);

	return 0;
}
#endif

