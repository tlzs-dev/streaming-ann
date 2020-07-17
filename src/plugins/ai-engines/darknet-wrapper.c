/*
 * darknet-wrapper.c
 * 
 * Copyright 2019 chehw <htc.chehw@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 * 
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "img_proc.h"
#include "utils.h"
#include "darknet-wrapper.h"

#include "darknet.h"
#include <cairo/cairo.h>

static const char * s_coco_names[80] = {
	"person",
	"bicycle",
	"car",
	"motorbike",
	"aeroplane",
	"bus",
	"train",
	"truck",
	"boat",
	"traffic light",
	"fire hydrant",
	"stop sign",
	"parking meter",
	"bench",
	"bird",
	"cat",
	"dog",
	"horse",
	"sheep",
	"cow",
	"elephant",
	"bear",
	"zebra",
	"giraffe",
	"backpack",
	"umbrella",
	"handbag",
	"tie",
	"suitcase",
	"frisbee",
	"skis",
	"snowboard",
	"sports ball",
	"kite",
	"baseball bat",
	"baseball glove",
	"skateboard",
	"surfboard",
	"tennis racket",
	"bottle",
	"wine glass",
	"cup",
	"fork",
	"knife",
	"spoon",
	"bowl",
	"banana",
	"apple",
	"sandwich",
	"orange",
	"broccoli",
	"carrot",
	"hot dog",
	"pizza",
	"donut",
	"cake",
	"chair",
	"sofa",
	"pottedplant",
	"bed",
	"diningtable",
	"toilet",
	"tvmonitor",
	"laptop",
	"mouse",
	"remote",
	"keyboard",
	"cell phone",
	"microwave",
	"oven",
	"toaster",
	"sink",
	"refrigerator",
	"book",
	"clock",
	"vase",
	"scissors",
	"teddy bear",
	"hair drier",
	"toothbrush",
	
};

typedef struct darknet_private
{
	darknet_context_t * darknet;
	network * net;
	json_object * jconfig;
	
	
	ssize_t labels_count;
	char ** labels;
	
	int relative;	// 1: return relative coordinates
	float thresh; 	// confidence threshold, default = 0.5f;
	float hier; 	// yolov2 only, default = 0.5f;
	float nms; 		// Non-maximum Suppression (NMS), default = 0.45;
}darknet_private_t;

darknet_private_t * darknet_private_new(darknet_context_t * darknet, json_object * jconfig)
{
	darknet_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->darknet = darknet;
	darknet->priv = priv;
	
	priv->jconfig = json_object_get(jconfig); // add ref
	
	debug_printf("jconfig: %s\n", json_object_to_json_string_ext(jconfig, JSON_C_TO_STRING_PRETTY));
	
	const char * cfg_file = json_get_value_default(jconfig, string, conf_file, "models/yolov3.cfg");
	const char * weights_file = json_get_value_default(jconfig, string, weights_file, "models/yolov3.weights");
	
#ifdef GPU
	int gpu_index = json_get_value_default(jconfig, int, gpu, -1);
	if(gpu_index >= 0) cuda_set_device(gpu_index);
#endif

	network * net = load_network((char *)cfg_file, (char *)weights_file, 0);
	assert(net);
	assert(net->n > 0);
	layer l = net->layers[net->n - 1];
	int num_classes = l.classes;
	assert(num_classes > 0 && num_classes < 10000);
	
	const char * labels_file = json_get_value(jconfig, string, labels_file);
	priv->labels_count = 80;
	priv->labels = (char **)s_coco_names;
		
	if(labels_file) {
		char ** labels = calloc(num_classes, sizeof(*labels));
		assert(labels);
		
		
		int count = 0;
		FILE * fp = fopen(labels_file, "r");
		assert(fp);
		
		char buf[4096] = "";
		char * line = NULL;
		while((line = fgets(buf, sizeof(buf) - 1, fp))) {
			char * p_comments = strchr(line, '#');
			if(p_comments) *p_comments = '\0';
			char * p_end = line + strlen(line);
			line = trim(line, p_end);
			
			int cb = strlen(line);
			if(cb == 0) continue;
			
			assert(count < num_classes);
			labels[count++] = strdup(line);
		}
		fclose(fp);
		
		priv->labels_count = count;
		priv->labels = labels;
	}
	assert(priv->labels_count == num_classes);
	set_batch_network(net, 1);
	
	priv->relative = json_get_value_default(jconfig, int, relative, 1);
	
	priv->thresh = json_get_value_default(jconfig, double, threshod, 0.5);
	priv->hier = json_get_value_default(jconfig, double, hier, 0.5);
	priv->nms = json_get_value_default(jconfig, double, nms, 0.45);
	
	priv->net = net;
	return priv;
}

static ssize_t darknet_predict(darknet_context_t * darknet, const bgra_image_t frame[1], ai_detection_t ** p_results);
darknet_context_t * darknet_context_new(json_object * jconfig, void * user_data)
{
	assert(jconfig && user_data);
	
	darknet_context_t * darknet = calloc(1, sizeof(*darknet));
	assert(darknet);
	
	darknet->user_data = user_data;
	darknet->predict = darknet_predict;
	
	darknet_private_t * priv = darknet_private_new(darknet, jconfig);
	assert(priv && darknet->priv == priv);
	
	return darknet;
}

void darknet_context_free(darknet_context_t * darknet)
{
	if(NULL == darknet) return;
	darknet_private_t * priv = darknet->priv;
	if(priv)
	{
		network * net = priv->net;
		if(net) free_network(net);
		priv->net = NULL;
		
		if(priv->labels && priv->labels != (char **)s_coco_names)
		{
			for(int i = 0; i < priv->labels_count; ++i) free(priv->labels[i]);
			
			free(priv->labels);
			priv->labels = NULL;
			priv->labels_count = 0;
		}
		
		if(priv->jconfig) json_object_put(priv->jconfig);
		free(priv);
		darknet->priv = NULL;
	}
	return;
}


static int bgra_image_to_f32(const bgra_image_t * restrict bgra, float * restrict dst)
{
	static const float scalar = 1.0f / 255.0f;
	assert(dst);

	ssize_t size = bgra->width * bgra->height;
	float * r_plane = dst;
	float * g_plane = r_plane + size;
	float * b_plane = g_plane + size;
	
	// from bgr (NHWC) to float32 (NCHW)
	const unsigned char * bgra_data = bgra->data;
	for(int pos = 0; pos < size; ++pos, bgra_data += 4)
	{
		r_plane[pos] = ((float) bgra_data[2]) * scalar;
		g_plane[pos] = ((float) bgra_data[1]) * scalar;
		b_plane[pos] = ((float) bgra_data[0]) * scalar;
	}
	return 0;
}

static bgra_image_t * bgra_image_resize(bgra_image_t * dst, int width, int height, const bgra_image_t * src)
{
	assert(src && width > 1 && height > 1 && src->width > 1 && src->height > 1 && src->data);
	cairo_surface_t * origin = cairo_image_surface_create_for_data((unsigned char *)src->data,
		CAIRO_FORMAT_ARGB32,
		src->width, src->height, src->width * 4);
	assert(origin);
	
	double sx = (double)width / (double)src->width;
	double sy = (double)height / (double)src->height;
	
	cairo_surface_t * resized = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
	assert(resized);
	
	cairo_t * cr = cairo_create(resized);
	assert(cr);
	
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, origin, 0, 0);
	cairo_paint(cr);
	
	cairo_destroy(cr);
	
	unsigned char * image_data = cairo_image_surface_get_data(resized);
	assert(image_data);
	
	dst = bgra_image_init(dst, width, height, image_data);
	assert(dst);
	
	cairo_surface_destroy(origin);
	cairo_surface_destroy(resized);
	return dst;
}


static ssize_t darknet_predict(darknet_context_t * darknet, const bgra_image_t frame[1], ai_detection_t ** p_results)
{
	darknet_private_t * priv = darknet->priv;
	network * net = priv->net;

	int width = net->w;
	int height = net->h;
	debug_printf("network size: %d x %d\n", width, height);
	debug_printf("resize: %d x %d   --> %d x %d\n", frame->width, frame->height, width, height);
	bgra_image_t * resized = bgra_image_resize(NULL, width, height, frame);
	assert(resized && (resized->width == width) && (resized->height == height) && resized->data);
	
	float * input = malloc(width * height * 3 * sizeof(float));
	assert(input);
	
	bgra_image_to_f32(resized, input);
	bgra_image_clear(resized); free(resized);
	
	network_predict(net, input);
	free(input);
	
	int count = 0;
	float thresh = priv->thresh;
	float hier = priv->hier;
	float nms = priv->nms;
	int relative = priv->relative;
	
	layer l = net->layers[net->n - 1];
	detection * dets = get_network_boxes(net, width, height, thresh, hier, NULL, relative, &count);
	
	ai_detection_t * results = calloc(count, sizeof(*results));
	assert(results);
	
	int dets_count = 0;
	if(dets && count > 0)
	{
		int num_classes = l.classes;
		do_nms_sort(dets, count, num_classes, nms);
		
		debug_printf("num_classes: %d\n", num_classes);
		assert(num_classes == priv->labels_count);
		// check confidence
		for(int i = 0; i < count; ++i)
		{
			char label[4096] = "";
			int klass = -1;
			int classes[num_classes];
			int confirmed_classes_count = 0;
			memset(classes, 0, num_classes * sizeof(classes[0]));
			
			float confidence = 0.0;
			for(int j = 0; j < num_classes; ++j)
			{
				if(dets[i].prob[j] > thresh)
				{
					if(klass < 0) // set main class
					{
						strcat(label, priv->labels[j]);
						klass = j;
						confidence = dets[i].prob[j];
					}else
					{
						strcat(label, ", ");
						strcat(label, priv->labels[j]); 
					}
					classes[confirmed_classes_count++] = j;
				}
			}
			
			if(klass >= 0)
			{
				printf("[%d]: confidence=%.3f, label=%s\n", dets_count, confidence, label);
				ai_detection_t * result = &results[dets_count++];
				
				memcpy(result->klass_list, classes, confirmed_classes_count * sizeof(int));
				
				// box: (center_pos + size)	-->  bounding box
				box b = dets[i].bbox;
				result->x = (b.x - b.w / 2.0);
				result->y = (b.y - b.h / 2.0);
				result->cx = b.w;
				result->cy = b.h;
				
				debug_printf("result: bbox:{%.3f, %.3f, %.3f, %.3f}\n", 
					result->x, result->y, result->cx, result->cy);
				
				strncpy(result->class_names, label, sizeof(result->class_names));
				result->confidence = confidence;
				result->klass = klass;
			}
		}
		
		free_detections(dets, count);
	}
	
	if(p_results) *p_results = results;
	return dets_count;
}


#if defined(_TEST_DARKNET_WRAPPER) && defined(_STAND_ALONE)
int main(int argc, char ** argv)
{
	darknet_context_t * darknet = darknet_context_new("cfg/yolov3.cfg", "models/yolov3.weights", NULL);
	assert(darknet);
	
	bgra_image_t frame[1];
	memset(frame, 0, sizeof(frame));
	
	int rc = bgra_image_load_from_file(frame, "1.jpg");
	assert(0 == rc);
	
	ai_detection_t * results = NULL;
	ssize_t count = darknet->predict(darknet, frame, &results);
	
	printf("detections count: %ld\n", (long)count);
	
	cairo_surface_t * png = cairo_image_surface_create_for_data(frame->data, 
		CAIRO_FORMAT_ARGB32, 
		frame->width, frame->height, frame->width * 4);
	assert(png);
	
	cairo_t * cr = cairo_create(png);
	cairo_set_line_width(cr, 3);
	cairo_set_source_rgb(cr, 1, 1, 0);
	for(int i = 0; i < count; ++i)
	{
		ai_detection_t * det = &results[i];
		double x = det->x * (double)frame->width;
		double y = det->y * (double)frame->height;
		double cx = det->cx * (double)frame->width;
		double cy = det->cy * (double)frame->height;
		
		printf("class: %s, bbox: {%.3f, %.3f, %.3f, %.3f}\n",
			det->class_names,
			x, y, cx, cy);
			
		cairo_rectangle(cr, x, y, cx, cy);
		cairo_stroke(cr);
	}
	cairo_destroy(cr);
	free(results);
	cairo_surface_write_to_png(png, "result.png");
	cairo_surface_destroy(png);
	
	darknet_context_free(darknet);
	return 0;
}
#endif
