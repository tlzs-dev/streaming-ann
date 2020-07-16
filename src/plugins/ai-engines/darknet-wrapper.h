#ifndef _DARKNET_WRAPPER_H_
#define _DARKNET_WRAPPER_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "img_proc.h"

#define MAX_AI_DETECTION_NAME_LEN (1024)
#define MAX_AI_DETECTION_CLASSES	(80)
typedef struct ai_detection
{
	char class_names[MAX_AI_DETECTION_NAME_LEN];		// multi-labels support
	int klass_list[MAX_AI_DETECTION_CLASSES];		
	
	int klass;				// main class
	float confidence;
	union
	{
		struct {float x, y, cx, cy;};
		struct {float x, y, cx, cy;} bbox;
	};
}ai_detection_t;

typedef struct darknet_context
{
	void * user_data;
	void * priv;

	int gpu_index;
	ssize_t (* predict)(struct darknet_context * darknet, const bgra_image_t frame[1], ai_detection_t ** p_results);
}darknet_context_t;

darknet_context_t * darknet_context_new(const char * cfg_file, const char * weights_file, const char * labels_file, void * user_data);
void darknet_context_free(darknet_context_t * darknet);





#ifdef __cplusplus
}
#endif
#endif

