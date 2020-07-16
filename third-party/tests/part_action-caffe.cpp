/*
 * part_action-caffe.cpp
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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <json-c/json.h>
#include <caffe/caffe.hpp>
#include <caffe/blob.hpp>

#include "img_proc.h"

static const char * s_model_cfg = "models/part_action_with_ctx_deploy.prototxt";
static const char * s_weights_file = "models/part_action_with_ctx_release.caffemodel";
//~ static const char * s_mean_file = "models/ResNet_mean.binaryproto";
//~ static const char * s_label_file = NULL;
 
static int bgra_to_float32_planes(
	int count, bgra_image_t * frames, 
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
		bgra_image_t * frame = &frames[i];
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


static int image_f32_crop(size_t count, const float * src,  int width,	// width == heigth 
	int cropped_size,
	float ** p_dst)
{
	static const int channels = 3;
	assert(count > 0 && src);
	assert(cropped_size > 0 && cropped_size < width);
	
	ssize_t src_plane_size = width * width;
	ssize_t src_size = src_plane_size * channels;
	const float * p_end = src + count * src_size;
	
	size_t dst_plane_size = cropped_size * cropped_size;
	size_t dst_size = dst_plane_size * channels;
	
	float * dst = (float *)malloc(count * sizeof(*dst) * dst_size);
	assert(dst);
	*p_dst = dst;
	
	int x_offset = (width - cropped_size) / 2;
	int y_offset = (width - cropped_size) / 2;
	
	for(size_t i = 0; i < count; ++i)
	{
		for(int c = 0; c < channels; ++c)
		{
			const float * row = src + y_offset * cropped_size + x_offset;
			for(int y = 0; y < cropped_size; ++y)
			{
				memcpy(dst, row, cropped_size * sizeof(*row));
				dst += cropped_size;
				row += width;
			}
			src += src_plane_size;
		}
	}
	
	assert(src == p_end);
	return 0;
}

static int image_f32_flip_vertical(size_t count, const float * src, int height, float ** p_dst)
{
	static const int channels = 3;
	assert(count > 0 && src && height > 1);
	
	size_t plane_size = height * height;
	
	float * dst = (float *)malloc(count * plane_size * channels * sizeof(*dst));
	assert(dst);
	*p_dst = dst;
	 
	size_t row_size = height; // width == heigth
	for(size_t i = 0; i < count; ++i)
	{
		for(int c = 0; c < channels; ++c)
		{
			for(int y = 0; y < height ; ++y) {
				memcpy(dst + y * row_size, src + (height -1 - y) * row_size, row_size * sizeof(*dst));
			}
			src += plane_size;
			dst += plane_size; 
		}
	}
	return 0;
}

static inline float calc_maximium(int n, const float X[/* n */]) {
	float x_max = -999999;
	for(int i = 0; i < n; ++i) {
		if(X[i] > x_max) x_max = X[i];
	}
	return x_max;
}
static inline void vec_add_scalar(int n, float X[/* n */], const float scalar)
{
	for(int i = 0; i < n; ++i) X[i] += scalar;
}

static void softmax(int n, const float X[/* n */], float Y[/* n */])
{
	double sum = 0.0;
	double epsilon = 0.00001;
	for(int i = 0; i < n; ++i) {
		Y[i] = exp(X[i]);
		sum += Y[i];
	}
	if(sum < epsilon) sum = epsilon;
	
	for(int i = 0; i < n; ++i) Y[i] /= epsilon;
	return;
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
	
	
	int num_channels = input_layer->channels();
	int width = input_layer->width();
	int height = input_layer->height();
	printf("input: %d x %d x %d x %d\n", (int)input_layer->shape(0),  num_channels, height, width);
	
	int num_outputs = (int)net.output_blobs().size();
	printf("num_outputs: %d\n", num_outputs);
	
	for(int i = 0; i < num_outputs; ++i) {
		caffe::Blob<float> * output_layer = net.output_blobs()[i];
		printf("output[%d]: %d x %d x %d x %d\n",
			i,
			(int)output_layer->shape(0),
			output_layer->channels(), 
			output_layer->height(), output_layer->width());
	}
	
	/**
	 * pre-process image
	 * ( convert python to C code )
	 * 
	 * https://github.com/ZhichenZhao/part-action-network/tree/master/test_stanford40
	 * config.py 
	 *     scales = [256, 240] 
	 *     cropped_size = 224
	 * 
	 * test.py
	 * # prepare transformers for multiscale test
	 *    transformers = []
	 *    def get_transformer(inp_shape):
	 *    	origin_shape = (9,3) + (inp_shape,inp_shape)
	 *    	transformer = caffe.io.Transformer({"data": origin_shape})
	 *    	transformer.set_transpose("data", (2,0,1))
	 *    	transformer.set_mean("data", np.array([128,128,128]))
	 *    	transformer.set_raw_scale("data", 255)
	 *    	transformer.set_channel_swap("data",(2,1,0))
	 *    	return transformer
	 *    
	 *    test_app = []
	 *    right = 0
	 *    wrong = 0
	 *    predictions = []
	 *    for s in scales:
	 *        transformers.append(get_transformer(s)) 
	 * 
	 *    pred_multiscale = []
	 *    tested_imgs = []
	 *    
	 *    # pre-process images by transformers but not resize or crop
	 *    for s_idx,s in enumerate(scales):
	 *    	for idx, im in enumerate(test_image):
	 *    		tested_imgs.append([])
	 *    		im_bbox = caffe.io.load_image(data_dir + bbox_dir + im)
	 *    		im_ctx = caffe.io.load_image(data_dir + ctx_dir + im)
	 *    		pics = []
	 *    		pics.append(transformers[s_idx].preprocess('data', im_bbox))
	 *    		pics.append(transformers[s_idx].preprocess('data', im_ctx))
	 *    		for part in ['head','torso','legs','larm','rarm','lhand','rhand']:
	 *    			im_name = data_dir + part_dir + im[:-4] + '_' + part + im[-4:]
	 *    			if os.path.exists(im_name):
	 *    				im_part = caffe.io.load_image(im_name)
	 *    			else:
	 *    				#warnings.warn(im_name + ' ' + part + ' lost!' + ', use mean image as filler')
	 *    				im_part = 0.5 * np.ones((256,256,3),dtype=float)
	 *    			pics.append(transformers[s_idx].preprocess('data', im_part)) 
	 *    		tested_imgs[s_idx].append(pics)
	 *    		print('loading image: ' + im)
	 * 
	 * 
	 *    # one-pass resize with crop and forward the Part Action Network
	 *    for i in range(len(test_image)):
	 *    	score = np.zeros(cls_len)
	 *    	for s_idx, s in enumerate(scales):
	 *    		print('processing image: ' + test_image[i] + ' at scale: ' + str(s))
	 *    		pics = tested_imgs[s_idx][i]
	 *    		pics = aug.crop_imgs_at_center(pics, s, cropped_size)
	 *    		pics_f1 = aug.aug_flip(pics)
	 *    
	 *    		prediction = utl.get_pred_by_batch(net, [pics,pics_f1], ['fc_stanford2','fc_part_act'], [1,0.2])
	 *    		score += prediction
	 *    	assert score.shape[0]==cls_len
	 *    	score /= len(scales)
	 *    	predictions.append(list(utl.softmax(score)))
	
	 */
	#define NUM_INPUTS (9)
	#define WIDTH (256)
	#define HEIGHT (256)
	static const int cropped_size = 224;
	float * inputs[2] = { NULL, NULL };
	
	bgra_image_t * frames = (bgra_image_t *)calloc(NUM_INPUTS, sizeof(*frames));
	
	// TODO: load images: in_bbox, im_ctx, ['head','torso','legs','larm','rarm','lhand','rhand']:
	for(int i = 0; i < NUM_INPUTS; ++i) {
		bgra_image_init(&frames[i], 640, 480, NULL); // skip load images
		memset(frames[i].data, 255, frames[i].width * frames[i].height * 4);
	}
	
	// pre-process images by transformers but not resize or crop
	float * input = NULL;
	int rc = bgra_to_float32_planes(NUM_INPUTS, frames, 
		WIDTH, HEIGHT, NULL, (1.0f / 255.0f),
		&input);
	assert((0 == rc) && input); 
	
	// one-pass resize with crop and forward the Part Action Network
	float * cropped = NULL;
	rc = image_f32_crop(NUM_INPUTS, input, WIDTH, cropped_size, &cropped);
	assert((0 == rc) && cropped);
	
	inputs[0] = cropped;
	
	float * flipped = NULL;
	rc = image_f32_flip_vertical(NUM_INPUTS, cropped, HEIGHT, &flipped);
	assert((0 == rc) && flipped);
	inputs[1] = flipped;
	
	/**
	 *  Predict: 
	 * 
		def get_pred_by_batch(net, imgs_in_batch, keys, coefs):
			prediction = [0]*cls_len
			sum_coefs = sum(coefs)
			sum_batch = len(imgs_in_batch)
			for imgs in imgs_in_batch:
				net.blobs['data'].data[...] = imgs
				out = net.forward()
				for idx, key in enumerate(keys):
					prediction_tmp = out[key][0]
					prediction = [x+coefs[idx]*y/sum_coefs/sum_batch for x,y in zip(prediction,prediction_tmp)]
			return prediction

		def softmax(x):
			e_x = np.exp(x - np.max(x))
			return e_x / e_x.sum()
	* 
		# calculate the mean accuracy and mean average precision
		mA = []
		preds = [pre.index(max(pre)) for pre in predictions]
		for cls in range(len(predictions[0])):
			label = [1 if y == cls else 0 for y in test_label]
			amt = sum(label)
			pr = [1 if pre == cls else 0 for pre in preds]
			tp = sum([1 if p==1 and l==1 else 0 for p,l in zip(pr,label)])
			acc = tp/(amt+0.0000001)
			print(categories_stanford40[cls] + ' accuracy: '+ str(acc))
			mA.append(acc)
		print('mA:')
		print sum(mA)/len(mA)
		mAP = 0
		for cls in range(len(predictions[0])):
			samples = [pred[cls] for pred in predictions]
			labels = [1 if y == cls else 0 for y in test_label]
			ap = average_precision_score(labels, samples)
			mAP += ap
			print(categories_stanford40[cls] + ' AP: ' + str(ap))

		print 'mAP:'
		print mAP/cls_len

	*/
	static const int sum_batch = 2;
	static const float coefs[2] = { 1.0f, 0.2f };
	#define NUM_CLASSES (40)
	static const char * keys[2] = { "fc_stanford2", "fc_part_act" };
	
	#define sum_array(n, X) ({ 							\
		float sum = 0.0;								\
		for(int i = 0; i < (int)n; ++i) sum += X[i];	\
		sum; \
	})
	float sum_coefs = sum_array(2, coefs);
	float prediction[NUM_CLASSES] = { 0 };
	
	// for (int k = 0; i < num_scales; ++k)
	for(int i = 0; i < 2; ++i)
	{
		input_layer->set_cpu_data(inputs[i]);
		//~ const std::vector<caffe::Blob<float>*> & result = net.ForwardPrefilled();
		//~ const boost::shared_ptr<caffe::Blob<float> > out1 = net.blob_by_name("fc_stanford2");
		//~ const boost::shared_ptr<caffe::Blob<float> > out2 = net.blob_by_name("fc_part_act");
		auto result = net.ForwardPrefilled();
		auto out1 = net.blob_by_name(keys[0]);
		auto out2 = net.blob_by_name(keys[1]);
	
		assert(out1 && out2);
		printf("out1: num_axes=%d, ", out1->num_axes());
		printf("dims: %d x %d \n", (int)out1->shape(0), out1->shape(1));
		
		printf("out2: num_axes=%d, ", out2->num_axes());
		printf("dims: %d x %d \n", out2->shape(0), out2->shape(1));
		
		const float * pred1 = out1->cpu_data();
		const float * pred2 = out2->cpu_data();
		
		for(ssize_t ii = 0; ii < NUM_CLASSES; ++ii) {
			prediction[ii] += coefs[0] * pred1[ii] / sum_coefs / sum_batch;
			prediction[ii] += coefs[1] * pred2[ii] / sum_coefs / sum_batch;
		}
	}
	float result[NUM_CLASSES] = { 0 };
	
	// softmax(pred[] - max_pred)
	int max_pred = calc_maximium(NUM_CLASSES, prediction);
	vec_add_scalar(NUM_CLASSES, prediction, -max_pred);
	softmax(NUM_CLASSES, prediction, result);
	
	std::vector<float> pred(prediction, prediction + NUM_CLASSES);
	for(int i = 0; i < NUM_CLASSES; ++i) {
		printf("class[%d]: confidence: %.3f\n", i, pred[i]);
	}
	// parse pred[]
	
	// cleanup
	free(inputs[0]);
	free(inputs[1]);
	for(int i = 0; i < NUM_INPUTS; ++i) {
		bgra_image_clear(&frames[i]); 
	}
	free(input);
	return 0;
}

