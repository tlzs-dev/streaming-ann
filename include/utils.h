#ifndef _UTILS_H_
#define _UTILS_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif


#include <inttypes.h>
#include <endian.h>

#ifndef _VERBOSE
#define _VERBOSE (0)
#endif

#ifndef debug_printf
#ifdef _DEBUG
#define debug_printf(fmt, ...) do { fprintf(stderr, "\e[33m%s(%d)::" fmt "\e[39m" "\n", __FILE__, __LINE__, ##__VA_ARGS__); } while(0)
#else
#define debug_printf(fmt, ...) do { } while(0)
#endif
#endif

#ifndef log_printf

#if defined(_VERBOSE) && (_VERBOSE > 1)
extern FILE * g_log_fp;
#define log_printf(fmt, ...) do { fprintf(g_log_fp?g_log_fp:stdout, "\e[32m" fmt "\e[39m" "\n", ##__VA_ARGS__); } while(0)
#else
#define log_printf(fmt, ...) do {  } while(0)
#endif
#endif


#define UNUSED(x)	(void)((x))

int check_file(const char * path_name);
int check_folder(const char * path_name, int auto_create);

ssize_t load_binary_data(const char * filename, unsigned char **p_dst);
ssize_t bin2hex(const unsigned char * data, size_t length, char * hex);
ssize_t hex2bin(const char * hex, size_t length, unsigned char * data);
char * trim_left(char * p_begin, char * p_end);
char * trim_right(char * p_begin, char * p_end);
#define trim(p, p_end)	 trim_right(trim_left(p, p_end), p_end)

typedef struct app_timer
{
	double begin;
	double end;
}app_timer_t;
double app_timer_start(app_timer_t * timer);
double app_timer_stop(app_timer_t * timer);
void global_timer_start();
void global_timer_stop(const char * prefix);







#include <json-c/json.h>

/**
 * @ingroup utils
 * @{
 */
typedef char * string;
#define json_get_value(jobj, type, key)	({									\
		type value = (type)0;												\
		if (jobj) {															\
			json_object * jvalue = NULL;									\
			json_bool ok = FALSE;											\
			ok = json_object_object_get_ex(jobj, #key, &jvalue);			\
			if(ok && jvalue) value = (type)json_object_get_##type(jvalue);	\
		}																	\
		value;																\
	})

#define json_get_value_default(jobj, type, key, defval)	({					\
		type value = (type)defval;											\
		json_object * jvalue = NULL;										\
		json_bool ok = FALSE;												\
		ok = json_object_object_get_ex(jobj, #key, &jvalue);				\
		if(ok && jvalue) value = (type)json_object_get_##type(jvalue);		\
		value;																\
	})
	
/**
 * @}
 */





 
#define BSWAP_16(x) ( 	((uint16_t)(x)  <<  8) | ((uint16_t)(x) >> 8) )
#define BSWAP_32(x) ( 	((uint32_t)(x)  << 24) | \
						(((uint32_t)(x) <<  8) & 0x00ff0000) | \
						(((uint32_t)(x) >>  8) & 0x0000ff00) | \
						((uint32_t)(x)  >> 24) )

#define	BSWAP_64(x) ( 	((uint64_t)(x)  << 56) | \
						(((uint64_t)(x) << 40) & 0xff000000000000ULL) | \
						(((uint64_t)(x) << 24) & 0x00ff0000000000ULL) | \
						(((uint64_t)(x) <<  8) & 0x0000ff00000000ULL) | \
						(((uint64_t)(x) >>  8) & 0x000000ff000000ULL) | \
						(((uint64_t)(x) >> 24) & 0x00000000ff0000ULL) | \
						(((uint64_t)(x) >> 40) & 0x0000000000ff00ULL) | \
						((uint64_t)(x)  >> 56))



#if __BYTE_ORDER == __LITTLE_ENDIAN
#define SER_UINT16(u16) BSWAP_16(u16)
#define SER_UINT32(u32) BSWAP_32(u32)
#define SER_UINT64(u64) BSWAP_64(u64)
#else
#define SER_UINT16(u16) (u16)
#define SER_UINT32(u32) (u32)
#define SER_UINT64(u64) (u64)
#error "big endian"
#endif

#define MAKE_USHORT_LE(c1, c2) (((unsigned short)(c1) & 0xFF) | (((unsigned short)(c2) & 0xFF) << 8))
#define MAKE_UINT32_LE(c1, c2, c3, c4) (((uint32_t)(c1)  & 0xFF ) | (((uint32_t)(c2) & 0xFF) << 8) \
										| (((uint32_t)(c3) & 0xFF) << 16)| (((uint32_t)(c4) & 0xFF) << 24) \
										)


size_t base64_encode(const void * src, size_t src_len, char ** p_dst);
size_t base64_decode(const char * src, size_t src_len, unsigned char ** p_dst);

#ifdef __cplusplus
}
#endif
#endif

