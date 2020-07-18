#ifndef _CAFFE_MODEL_PLUGIN_H_
#define _CAFFE_MODEL_PLUGIN_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "img_proc.h"
typedef struct caffe_tensor {
	union {
		struct { int n, c, h, w; }; 	// dims
		int dims[4];
	};
	float * data;
	char * name;	// same as the parameter for output->blob_by_name(name);
}caffe_tensor_t;
caffe_tensor_t * caffe_tensor_init(caffe_tensor_t * tensor, const char * name, const int dims[],  float * data);
void caffe_tensor_cleanup(caffe_tensor_t * tensor);

typedef struct caffe_model_plugin
{
	void * user_data;
	void * priv;

	int gpu_index;
	ssize_t (* predict)(struct caffe_model_plugin * plugin, const bgra_image_t frame[], caffe_tensor_t ** p_results);
	
	ssize_t (* pre_process) (struct caffe_model_plugin * plugin, const bgra_image_t frame[], caffe_tensor_t ** p_input, void * input_layer);
	ssize_t (* post_process)(struct caffe_model_plugin * plugin, ssize_t num_outputs, const caffe_tensor_t * raw_outputs[], caffe_tensor_t ** p_outputs, void * user_data);
}caffe_model_plugin_t;

caffe_model_plugin_t * caffe_model_plugin_new(json_object * jconfig, void * user_data);
void caffe_model_plugin_free(caffe_model_plugin_t * plugin);

int caffe_model_set_pre_process(caffe_model_plugin_t * plugin, 
	ssize_t (* callback)(caffe_model_plugin_t * plugin, const bgra_image_t frame[], caffe_tensor_t ** p_input, void * input_layer));
int caffe_model_set_post_process(caffe_model_plugin_t * plugin, 
	ssize_t (*callback)(caffe_model_plugin_t * plugin, ssize_t num_outputs, const caffe_tensor_t * raw_outputs[], caffe_tensor_t ** p_outputs));

#ifdef __cplusplus
}
#endif
#endif
