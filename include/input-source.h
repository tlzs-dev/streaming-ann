#ifndef _INPUT_SOURCE_H_
#define _INPUT_SOURCE_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "img_proc.h"

/**
 * @ingroup input_source
 * @{
 */

#include "input-frame.h"
 

/**
 * @ingroup input_source
 * @{
 */
enum input_source_type
{
	input_source_type_unknown, 
	input_source_type_rtsp,
	input_source_type_http,
	input_source_type_https,
	input_source_type_file,
	input_source_type_v4l2,
	input_source_type_fsnotify,
	input_source_types_count
};
enum input_source_subtype
{
	input_source_subtype_default,
	input_source_subtype_video,
	input_source_subtype_jpeg,
	input_source_subtype_png,
	input_source_subtype_hls,
	input_source_subtype_json_flags = 0x8000,
};
enum input_source_type input_source_type_parse_uri(const char * uri, char ** p_cooked_uri, int * subtype);
/**
 * @}
 */


/**
 * @ingroup input_source
 * @{
 */
typedef struct input_source
{
	void * user_data;
	void * priv;

	enum input_source_type 		type;
	enum input_source_subtype 	subtype;

	int (* set_uri)(struct input_source * input, const char * uri);

	int (* play)(struct input_source * input);
	int (* stop)(struct input_source * input);
	int (* pause)(struct input_source * input);
	int (* restart)(struct input_source * input);
	
	int (* on_new_frame)(struct input_source * input, const input_frame_t * frame);

	long (* set_frame)(struct input_source * input, const input_frame_t * frame);
	long (* get_frame)(struct input_source * input, long prev_frame, input_frame_t * frame);
}input_source_t;
input_source_t * input_source_new(void * user_data);
//~ input_source_t * input_source_new_from_uri(const char * uri, void * user_data);
#define input_source_new_from_uri(uri, user_data) ({				\
		input_source_t * input = input_source_new(user_data);		\
		assert(input);												\
		int rc = input->set_uri(input, uri);						\
		if(rc){ input_source_cleanup(input); input = NULL; }		\
		input;														\
	})
void input_source_cleanup(input_source_t * input);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif
#endif

