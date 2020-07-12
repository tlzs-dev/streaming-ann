/*
 * resnet-caffe.cpp
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
#include <string>
#include <assert.h>
#include <cairo/cairo.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <json-c/json.h>
#include <caffe/caffe.hpp>
#include <caffe/blob.hpp>

static const char * s_model_cfg = "models/ResNet-101-deploy.prototxt";
static const char * s_weights_file = "models/ResNet-101-model.caffemodel";
static const char * s_mean_file = "models/ResNet_mean.binaryproto";
//~ static const char * s_label_file = NULL;
 
static inline int bgra_to_float32_planes(
	const unsigned char * frame, int image_width, int image_height,
	int width, int height, // net.dims 
	const float * means,  
	float ** p_rgb_planes)
{
	assert(image_width > 1 && image_height > 1);
	assert(width > 1 && height > 1);
	
	cairo_surface_t * image = NULL;
	const unsigned char * image_data = frame;
	if(image_width != width || image_height != height)	// resize image
	{
		cairo_surface_t * surface = cairo_image_surface_create_for_data(
			(unsigned char *)frame, 
			CAIRO_FORMAT_ARGB32, 
			image_width, image_height,
			image_width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
		assert(image && cairo_surface_status(image) == CAIRO_STATUS_SUCCESS);
		cairo_t * cr = cairo_create(image);

		double sx = (double)width / (double)image_width;
		double sy = (double)height / (double)image_height;
		cairo_scale(cr, sx, sy);
		cairo_set_source_surface(cr, surface, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		
		cairo_surface_destroy(surface);
		image_data = cairo_image_surface_get_data(image);
	}
	
	float * rgb_planes = (float *)malloc(sizeof(float) * 3 * width * height);
	ssize_t size = width * height;
	
	float * r_plane = rgb_planes;
	float * g_plane = r_plane + size;
	float * b_plane = g_plane + size;
	
	const unsigned char * data = image_data;
	for(ssize_t i = 0; i < size; ++i)
	{
		r_plane[i] = (float)data[2] - means[0];
		g_plane[i] = (float)data[1] - means[1];
		b_plane[i] = (float)data[0] - means[2];
		data += 4;
		means += 3;
	}
	if(image) cairo_surface_destroy(image);
	
	*p_rgb_planes = rgb_planes;
	return 0;
}

int main(int argc, char **argv)
{
#ifndef _GPU
	caffe::Caffe::set_mode(caffe::Caffe::CPU);
#else
	caffe::Caffe::set_mode(caffe::Caffe::GPU);
#endif

	gflags::SetVersionString(AS_STRING(CAFFE_VERSION));
	gflags::SetUsageMessage("Usuage: %s [command] [args]\n"
		"commands: \n"
		"    test        predict\n");
	caffe::GlobalInit(&argc, &argv);
	
	caffe::Net<float> net(s_model_cfg, caffe::TEST);
	net.CopyTrainedLayersFrom(s_weights_file);
	
	caffe::Blob<float> * input_layer = net.input_blobs()[0];
	caffe::Blob<float> * output_layer = net.output_blobs()[0];
	
	int num_channels = input_layer->channels();
	int width = input_layer->width();
	int height = input_layer->height();
	printf("input: %d x %d x %d\n", num_channels, height, width);
	printf("output: %d x %d x %d\n", 
		output_layer->channels(), 
		output_layer->height(), output_layer->width());
	
	caffe::Blob<float> mean_blob;
	caffe::BlobProto mean_proto;
	caffe::ReadProtoFromBinaryFileOrDie(s_mean_file, &mean_proto);
	
	mean_blob.FromProto(mean_proto);
	
	printf("mean_blob dims: %d x %d x %d\n", 
		mean_blob.channels(),
		mean_blob.width(),
		mean_blob.height()
	);
	assert(mean_blob.channels() == num_channels);
	
	cairo_surface_t * png = cairo_image_surface_create_from_png("1.png");
	assert(png && cairo_surface_status(png) == CAIRO_STATUS_SUCCESS);
	const unsigned char * image_data = cairo_image_surface_get_data(png);
	int image_width = cairo_image_surface_get_width(png);
	int image_height = cairo_image_surface_get_height(png);
	
	
	float * input = NULL;
	int rc = bgra_to_float32_planes(image_data, image_width, image_height,
		width, height, 
		mean_blob.cpu_data(),
		&input);
	assert(0 == rc);
	cairo_surface_destroy(png);
	
	input_layer->set_cpu_data(input);
	
	const std::vector<caffe::Blob<float>*> & results = net.ForwardPrefilled();
	printf("num_outputs: %d\n", (int)results.size());
	
	caffe::Blob<float> * det = results[0];
	printf("output_dim: %d x %d x %d\n", det->channels(), det->width(), det->height());
	
	free(input);
	return 0;
}

