/*
 * caffe-wrapper.cpp
 * 
 * Copyright 2020 Che Hongwei <htc.chehw@gmail.com>
 * 
 * The MIT License (MIT)
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to deal 
 * in the Software without restriction, including without limitation the rights 
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
 * copies of the Software, and to permit persons to whom the Software is 
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all 
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE.
 * 
 */


#include <iostream>
#include <vector>
#include <algorithm>    // std::sort
#include <string>
#include <assert.h>
#include <cairo/cairo.h>

//~ #include <gflags/gflags.h>
//~ #include <glog/logging.h>

#include <json-c/json.h>
#include <caffe/caffe.hpp>
#include <caffe/blob.hpp>

#include "img_proc.h"
#include "utils.h"

#include "caffe-model.h"
#include "ai-engine.h"
#include "input-frame.h"

extern "C" {
static ssize_t caffe_model_predict(struct caffe_model_plugin * plugin, const bgra_image_t frame[], caffe_tensor_t ** p_results);
}

struct caffe_model_private
{
	caffe_model_plugin_t * plugin;
	caffe::Net<float> net;
	json_object * jconfig;
	
	const char * cfg_file;
	const char * weights_file;
	const char * means_file;
	
	
	int has_means_blob;
	caffe::Blob<float> means_blob;
	
	int has_means_scalar;
	std::vector<float> means;
	
	float value_scale;	// value range: [ 0 .. <value_scale> ]
	int has_output_names;
	std::vector<const char *> output_names;
protected:
	caffe_model_private() = delete;
public:
	caffe_model_private(caffe_model_plugin_t * _plugin, const char * _cfg_file, const char * _weights_file = NULL ): 
		plugin(_plugin), net(_cfg_file, caffe::TEST), means_file(NULL),
		has_means_blob(0),
		has_means_scalar(0),
		value_scale(1.0f),
		has_output_names(0)
	{
		cfg_file = _cfg_file;
		weights_file = _weights_file;
		means_file = NULL;
		if(weights_file) net.CopyTrainedLayersFrom(weights_file);
	}
	
	int load_config(json_object * _jconfig) {
		jconfig = json_object_get(_jconfig);
		
		debug_printf("jconfig: %s\n", json_object_to_json_string_ext(jconfig, JSON_C_TO_STRING_PRETTY));
		
		json_bool ok = FALSE;
		if(NULL == weights_file) {
			json_object * jobj = NULL;
			ok = json_object_object_get_ex(jconfig, "weights_file", &jobj);
			if(ok && jobj) {
				weights_file = json_object_get_string(jobj);
			}
		}
		
		if(NULL == means_file) {
			json_object * jobj = NULL;
			ok = json_object_object_get_ex(jconfig, "means_file", &jobj);
			if(ok && jobj) {
				means_file = json_object_get_string(jobj);
			}
		}
		assert(weights_file);
		
		if(weights_file) net.CopyTrainedLayersFrom(weights_file);
		if(means_file) {
			has_means_blob = 1;
			caffe::BlobProto means_proto;
			caffe::ReadProtoFromBinaryFileOrDie(means_file, &means_proto);
			means_blob.FromProto(means_proto);
		}
		
		json_object * jmeans = NULL;
		ok = json_object_object_get_ex(jconfig, "means", &jmeans);
		if(ok && jmeans)
		{
			int count = json_object_array_length(jmeans);
			has_means_scalar = (count > 0);
			for(int i = 0; i < count; ++i) {
				means.push_back((float)json_object_get_double(json_object_array_get_idx(jmeans, i)));
			}
		}
		
		if(!has_means_scalar) {
			means.push_back(128.0f);
			means.push_back(128.0f);
			means.push_back(128.0f);
		}
		
		json_object * joutputs = NULL;
		ok = json_object_object_get_ex(jconfig, "outputs", &joutputs);
		if(ok && joutputs)
		{
			int count = json_object_array_length(jmeans);
			has_output_names = (count > 0);
			for(int i = 0; i < count; ++i) {
				output_names.push_back((const char *)json_object_get_string(json_object_array_get_idx(joutputs, i)));
			}
		}
		
		json_object * jvalue_scale = NULL;
		ok = json_object_object_get_ex(jconfig, "value_scale", &jvalue_scale);
		if(ok && jvalue_scale) value_scale = json_object_get_double(jvalue_scale);
		
		return 0;
	}
};


caffe_model_plugin_t * caffe_model_plugin_new(json_object * jconfig, void * user_data)
{
	assert(jconfig);
	
#ifdef CPU_ONLY
	caffe::Caffe::set_mode(caffe::Caffe::CPU);
#else
	caffe::Caffe::set_mode(caffe::Caffe::GPU);
#endif

	gflags::SetVersionString(AS_STRING(CAFFE_VERSION));
	
	static int argc = 1;
	static const char * args[] = { "caffe-model", NULL };
	char ** argv = (char **)args;
	caffe::GlobalInit(&argc, &argv);
	
	caffe_model_plugin_t * plugin = (caffe_model_plugin_t *)calloc(1, sizeof(*plugin));
	assert(plugin);
	
	plugin->user_data = user_data;
	plugin->predict = caffe_model_predict;
	
	json_object * jcfg_file = NULL;
	json_bool ok = json_object_object_get_ex(jconfig, "conf_file", &jcfg_file);
	assert(ok && jcfg_file);
	const char * cfg_file = json_object_get_string(jcfg_file);
	assert(cfg_file && cfg_file[0]);
	
	caffe_model_private * priv = new caffe_model_private(plugin, cfg_file);
	assert(priv);
	
	plugin->priv = priv;
	int rc = priv->load_config(jconfig);
	assert(0 == rc);
	
	return plugin;
}

void caffe_model_plugin_free(caffe_model_plugin_t * plugin)
{
	if(NULL == plugin) return;
	caffe_model_private * priv = (caffe_model_private *)plugin->priv;
	
	delete(priv);
	free(plugin);
}

static int bgra_to_float32_planes(
	int count, const bgra_image_t * frames, 
	int width, int height, // net.dims 
	const float * means,  
	const float value_scale,
	float ** p_rgb_planes)
{
	assert(frames);
	assert(width > 1 && height > 1);
	if(count <= 0) return -1;
	
	ssize_t dst_size = width * height * 3;
	float * dst = (float *)malloc(sizeof(*dst) * dst_size * count);
	assert(dst);
	*p_rgb_planes = dst;
	
	float * rgb_planes = dst;
	for(int i = 0; i < count; ++i)
	{
		const bgra_image_t * frame = &frames[i];
		assert(frame->data && frame->width > 1 && frame->height > 1);
		
		cairo_surface_t * image = NULL;
		const unsigned char * image_data = frame->data;
		if(frame->width != width || frame->height != height)	// resize image
		{
			cairo_surface_t * surface = cairo_image_surface_create_for_data(
				(unsigned char *)frame, 
				CAIRO_FORMAT_ARGB32, 
				frame->width, frame->height,
				frame->width * 4);
			assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
			
			image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
			assert(image && cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
			cairo_t * cr = cairo_create(image);

			double sx = (double)width / (double)frame->width;
			double sy = (double)height / (double)frame->height;
			cairo_scale(cr, sx, sy);
			cairo_set_source_surface(cr, surface, 0, 0);
			cairo_paint(cr);
			cairo_destroy(cr);
			
			cairo_surface_destroy(surface);
			image_data = cairo_image_surface_get_data(image);
		}
		

		ssize_t size = width * height;
		
		float * r_plane = rgb_planes;
		float * g_plane = r_plane + size;
		float * b_plane = g_plane + size;
		
		const unsigned char * data = image_data;
		if(means) {
			for(ssize_t ii = 0; ii < size; ++ii)
			{
				r_plane[ii] = ((float)data[2] - means[0]) * value_scale;
				g_plane[ii] = ((float)data[1] - means[1]) * value_scale;
				b_plane[ii] = ((float)data[0] - means[2]) * value_scale;
				data += 4;
				means += 3;
			}
		}else {
			for(ssize_t ii = 0; ii < size; ++ii)
			{
				r_plane[ii] = ((float)data[2] - 128.0f) * value_scale;
				g_plane[ii] = ((float)data[1] - 128.0f) * value_scale;
				b_plane[ii] = ((float)data[0] - 128.0f) * value_scale;
				data += 4;
			}
		}
		if(image) cairo_surface_destroy(image);
		image = NULL;
		
		rgb_planes += dst_size;
	}
	
	return 0;
}

static inline void caffe_tensor_set_output(caffe_tensor_t * result, const char * name, const caffe::Blob<float>* out) {
	assert(name && result && out);
	result->name = strdup(name);
	
	// set dims
	int shape_size = out->num_axes();
	assert(shape_size <= 4);
	int ii = 0;
	size_t size = 1;
	for(; ii < shape_size; ++ii) {
		result->dims[ii] = out->shape(ii);
		size *= result->dims[ii];
	}
	for(; ii < 4; ++ii) result->dims[ii] = 1;
	
	// set data
	assert(size > 0);
	result->data = (float *)malloc(size * sizeof(*result->data));
	assert(result->data);
	memcpy(result->data, out->cpu_data(), size * sizeof(*result->data));
	
#ifdef _DEBUG
	fprintf(stderr, "%s()...\n", __FUNCTION__);
	for(int i = 0; i < size; ++i) {
		if((i % 16) == 0) printf("\n");
		fprintf(stderr, "%.6f, ", result->data[i]);
	}
#endif
	return;
}

static ssize_t caffe_model_predict(struct caffe_model_plugin * plugin, const bgra_image_t frames[], caffe_tensor_t ** p_results)
{
	caffe_model_private * priv = (caffe_model_private *)plugin->priv;
	assert(priv);
	caffe::Net<float> &net = priv->net;
	
	caffe::Blob<float> * input_layer = net.input_blobs()[0];
	
	int n = input_layer->shape(0);
	assert(n >= 1);
	
	int width = input_layer->width();
	int height = input_layer->height();
	
	// data transform
	caffe_tensor_t *input = NULL;
	float * input_data = NULL;
	if(plugin->pre_process) {
		plugin->pre_process(plugin, frames, &input, input_layer);
		if(input) input_data = input->data;
	}
	else {
		bgra_to_float32_planes(n, frames, width, height, 
			priv->has_means_blob?priv->means_blob.cpu_data():NULL, 
			priv->value_scale,
			&input_data);
		assert(input_data);
	}
	
	// predict
	input_layer->set_cpu_data(input_data);
	const std::vector<caffe::Blob<float>*> & outputs = net.ForwardPrefilled();
	
	if(input) caffe_tensor_cleanup(input);
	else if(input_data) { free(input_data); };
	
	// output
	caffe_tensor_t * results = NULL;
	ssize_t num_outputs = priv->output_names.size();
	
	if(num_outputs > 0)
	{
		results = (caffe_tensor_t *)calloc(num_outputs, sizeof(*results));
		assert(results);
		
		for(int i = 0; i < num_outputs; ++i) {
			caffe_tensor_t * result = &results[i];
			const char * name = priv->output_names[i];
			assert(name);
			const caffe::Blob<float>* out = net.blob_by_name(name).get();
			caffe_tensor_set_output(result, name, out);
		}
		*p_results = results;
		
		return num_outputs;
	}
	
	num_outputs = outputs.size();
	if(num_outputs <= 0) return 0;
	
	results = (caffe_tensor_t *)calloc(num_outputs, sizeof(*results));
	assert(results);
		
	for(int i = 0; i < num_outputs; ++i) {
		caffe_tensor_t * result = &results[i];
		char name[100] = "";
		snprintf(name, sizeof(name), "output_%d", i);

		auto out = outputs[i];
		caffe_tensor_set_output(result, name, out);
		
	}
	*p_results = results;
	return num_outputs;
}
#if defined(_TEST_CAFFE_MODEL) && defined(_STAND_ALONE)
int main(int argc, char **argv)
{
	
	return 0;
}
#endif

