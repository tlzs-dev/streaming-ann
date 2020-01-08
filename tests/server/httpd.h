#ifndef _HTTPD_H_
#define _HTTPD_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @ingroup output-module
 * @{
 */
typedef struct http_auto_buffer
{
	unsigned char * data;
	size_t size;
	size_t length;
	size_t cur_pos;
	
	ssize_t (* push)(struct http_auto_buffer * buf, const void * data, size_t size);
	ssize_t (* peek)(struct http_auto_buffer * buf, unsigned char ** p_dst, size_t size);

	int (* resize)(struct http_auto_buffer * buf, size_t new_size);
	int (* set_pos)(struct http_auto_buffer * buf, ssize_t new_pos);
}http_auto_buffer_t;
#define http_auto_buffer_reset(buf)  do { buf->length = 0; buf->cur_pos = 0; } while(0)
http_auto_buffer_t * auto_buffer_init(http_auto_buffer_t * buf, size_t max_size);
void auto_buffer_cleanup(http_auto_buffer_t * buf);
//~ ssize_t auto_buffer_push_data(auto_buffer_t * buf, const void * data, size_t size);
//~ ssize_t auto_buffer_pop_data(auto_buffer_t * buf, unsigned char ** p_dst, size_t dst_size);
/**
 * @}
 */

/**
 * @ingroup output-module
 * @{
 */
enum http_stage
{
	http_stage_need_data = -100,
	http_stage_unknown = -1,
	http_stage_init,
	http_stage_parse_header,
	http_stage_read_data,
	http_stage_request_final,
	http_stage_write_header,
	http_stage_write_data,
	http_stage_response_final,
	http_stage_cleanup,
};
/**
 * @}
 */
 
/**
 * @ingroup output-module
 * @{
 */
typedef struct http_header_field
{
	char * key;
	char * value;
}http_header_field_t;
int http_header_field_set(http_header_field_t * field, const char * key, const char * value);
void http_header_field_cleanup(http_header_field_t * field);

/**
 * @ingroup output-module
 * @{
 */
typedef struct http_header
{
	long response_code;
	enum http_stage stage;		// current parsed stage
	
	http_header_field_t ** fields;
	size_t max_fields;
	size_t fields_count;
	
	char * method;
	char * path;
	char * protocol;
	char * query_string;

	long content_length;
	
	int (* set)(struct http_header * hdr, const char * key, const char * value);
	const char * (* get)(struct http_header * hdr, const char * key);
	int (* remove)(struct http_header * hdr, const char * key);
	int (* clear)(struct http_header * hdr);
	
	int (* sort)(struct http_header * hdr);
	const char * (* to_string)(struct http_header * hdr, int sort);

	enum http_stage (* parse)(struct http_header * hdr, http_auto_buffer_t * buf);
	
	/* private data */
	http_auto_buffer_t buffer[1];
	void * fields_root;		// tsearch root
}http_header_t;
http_header_t * http_header_init(http_header_t * hdr, size_t max_fields);
void http_header_cleanup(http_header_t * hdr);
/**
 * @}
 */

/**
 * @ingroup tcp_server
 * @{
 */
typedef struct tcp_server
{
	int fd;		// server_fd
	int efd;	// epoll fd

	void * user_data;
	void * priv;
	int (* on_accept)(struct tcp_server * tcp, void * user_data);
	int (* on_error)(struct tcp_server * tcp, void * user_data);
}tcp_server_t;
tcp_server_t * tcp_server_init(tcp_server_t * tcp, void * user_data);
void tcp_server_cleanup(tcp_server_t * tcp);

/**
 * @}
 */

/**
 * @ingroup http_session		peer_info
 * @{
 */
typedef struct http_session
{
	struct http_server * server;
	void * user_data;
	
	int fd;		// client_fd
	int quit;
	
	int async_mode;		// 0: event (edge_trigger) mode ; 1: thread mode
	pthread_t th;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	
	enum http_stage stage;
	http_header_t request_hdr[1];
	http_header_t response_hdr[1];
	
	http_auto_buffer_t in_buf[1];
	http_auto_buffer_t out_buf[1];
	
	enum http_stage (* on_read)(struct http_session * session, void * user_data);			// can read 		(read-end avaliable)
	enum http_stage (* on_write)(struct http_session * session, void * user_data);			// can write		(write-end avaliable)
	int (* on_error)(struct http_session * session, void * user_data);

	int (* on_request)(struct http_session * session);
	int (* on_response)(struct http_session * session);

}http_session_t;
http_session_t * http_session_new(struct tcp_server * server, int peer_fd, int async_mode, void * user_data);
void http_session_free(http_session_t * session);
/**
 * @}
 */


/**
 * @ingroup http_server
 * @{
 */
typedef struct http_server
{
	tcp_server_t base[1];
	void * user_data;
	void * priv;
	
	http_session_t ** sessions;
	size_t max_sessions;
	size_t sessions_count;
	
	void * sessions_root;	// tsearch root
	int quit;
	
	int local_only;			// only accept connections from localhost or via ssh-tunnel, (no need to use SSL) 
	int use_ssl;
	char * ca_file;			// if use private CA 
	char * key_file;		// privkey / cert stored seperately
	char * cert_file;		// server cert.
	
	int async_mode;
	pthread_t th;
	pthread_mutex_t mutex;
	
	int (* run)(struct http_server * http, int async_mode);		// async_mode: Server Runing Mode. 0 == blocked; 1 == thread mode
	int (* stop)(struct http_server * http);

	int (* add_session)(struct http_server * http, 		http_session_t * session);
	int (* remove_session)(struct http_server * http, 	http_session_t * session);


	/* custom callbacks */
	int (* on_request)(http_session_t * session);
	int (* on_response)(http_session_t * session);
}http_server_t;
http_server_t * http_server_init(http_server_t * http, const char * host_name, const char * port, int local_only);
void http_server_cleanup(http_server_t * http);
/**
 * @}
 */
 
#ifdef __cplusplus
}
#endif
#endif

