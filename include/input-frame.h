#ifndef _INPUT_FRAME_H_
#define _INPUT_FRAME_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "img_proc.h"



enum input_frame_type
{
	input_frame_type_invalid = -1,
	input_frame_type_unknown = 0,
	input_frame_type_bgra = 1,
	input_frame_type_jpeg = 2,
	input_frame_type_png  = 3,

	input_frame_type_image_masks = 0x7FFF,
	input_frame_type_json_flag = 0x8000,
};

enum input_frame_type input_frame_type_from_string(const char * sz_type);
ssize_t	input_frame_type_to_string(enum input_frame_type type, char sz_type[], size_t size);


typedef struct input_frame
{
	union
	{
		bgra_image_t bgra[1];
		bgra_image_t image[1];
		struct
		{
			unsigned char * data;
			int width;
			int height;
			int channels;
			int stride;
		};
	};
	ssize_t length;
	
	long frame_number;
	struct timespec timestamp[1];
	enum input_frame_type type;
	
	ssize_t cb_json;
//	union
//	{
		char * json_str;
		void * meta_data;	// json_object
//	};
}input_frame_t;
void input_frame_free(input_frame_t * frame);
input_frame_t * input_frame_new();
void input_frame_clear(input_frame_t * frame);

int input_frame_set_bgra(input_frame_t * input, const bgra_image_t * bgra, const char * json_str, ssize_t cb_json);
int input_frame_set_jpeg(input_frame_t * input, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json);
int input_frame_set_png(input_frame_t * input, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json);

//~ int input_frame_set_data(input_frame_t * input, int type,
	//~ const unsigned char * data, ssize_t length,
	//~ int width, int height, int channels, int stride,
	//~ const char * json_str, ssize_t cb_json);
input_frame_t * input_frame_copy(input_frame_t * dst, const input_frame_t * src);

/*********************************
 * input_frame
 *********************************/


#ifdef __cplusplus
}
#endif

#endif
