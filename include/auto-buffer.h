#ifndef _AUTO_BUFFER_H_
#define _AUTO_BUFFER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct auto_buffer
{
	unsigned char * data;
	ssize_t max_size;
	ssize_t length;
	ssize_t cur_pos;
}auto_buffer_t;

auto_buffer_t * auto_buffer_init(auto_buffer_t * buf, ssize_t max_size);
void auto_buffer_cleanup(auto_buffer_t * buf);
int auto_buffer_resize(auto_buffer_t * buf, ssize_t new_size);
ssize_t auto_buffer_push_data(auto_buffer_t * buf, const void * data, ssize_t length);
ssize_t auto_buffer_peek_data(auto_buffer_t * buf, void * data, ssize_t length);
ssize_t auto_buffer_pop_data(auto_buffer_t * buf, void * data, ssize_t length);
#define auto_buffer_seek(buf, offset, whence) do { switch(whence) { \
		case SEEK_SET: buf->cur_pos = offset; break;		\
		case SEEK_CUR: buf->cur_pos += offset; break;		\
		case SEEK_END: buf->cur_pos = buf->length; break;	\
		default; break; }\
		if(buf->cur_pos < 0) { buf->cur_pos = 0; }	\
		if(buf->cur_pos > buf->length) buf->length = buf->cur_pos; \
	} while(0)

#define auto_buffer_reset(buf)	do { buf->cur_pos = 0; buf->length = 0; } while(0)

#ifdef __cplusplus
}
#endif
#endif
