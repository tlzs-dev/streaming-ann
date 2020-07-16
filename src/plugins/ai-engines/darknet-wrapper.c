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
	network * net;
	
	ssize_t labels_count;
	char ** labels;
}darknet_private_t;

static ssize_t darknet_predict(darknet_context_t * darknet, const bgra_image_t frame[1], ai_detection_t ** p_results);

darknet_context_t * darknet_context_new(const char * cfg_file, const char * weights_file, const char * labels_file, void * user_data)
{
	darknet_context_t * darknet = calloc(1, sizeof(*darknet));
	assert(darknet);
	
	darknet->user_data = user_data;
	
	darknet_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	darknet->priv = priv;
	
	network * net = load_network((char *)cfg_file, (char *)weights_file, 0);
	assert(net);
	priv->net = net;
	
	darknet->predict = darknet_predict;
	set_batch_network(net, 1);
	
	assert(net->n > 0);
	layer l = net->layers[net->n - 1];
	int num_classes = l.classes;
	assert(num_classes > 0 && num_classes < 10000);
	
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
			labels[count++] = strdup(line);
		}
		fclose(fp);
		
		priv->labels_count = count;
		priv->labels = labels;
	}
	
	assert(priv->labels_count == num_classes);
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
		
		if(priv->labels && priv->labels != (char **)s_coco_names)
		{
			for(int i = 0; i < priv->labels_count; ++i) free(priv->labels[i]);
			
			free(priv->labels);
			priv->labels = NULL;
			priv->labels_count = 0;
		}
		
		darknet->priv = NULL;
		
	}
	return;
}


static inline int bgra_image_to_f32(const bgra_image_t * restrict bgra, float * restrict dst)
{
	assert(dst);
	
	int size = bgra->width * bgra->height;
	float * r_plane = dst;
	float * g_plane = r_plane + size;
	float * b_plane = g_plane + size;
	
	for(int pos = 0; pos < size; ++pos)
	{
		unsigned char * pixel = &bgra->data[pos * 4];
		r_plane[pos] = ((float) pixel[2]) / 255.0;
		g_plane[pos] = ((float) pixel[1]) / 255.0;
		b_plane[pos] = ((float) pixel[0]) / 255.0;
	}
	return 0;
}

static int bgra_image_resize(bgra_image_t * dst, int width, int height, const bgra_image_t * src)
{
	assert(dst && src && width > 1 && height > 1 && src->width > 1 && src->height > 1 && src->data);
	cairo_surface_t * origin = cairo_image_surface_create_for_data((unsigned char *)src->data,
		CAIRO_FORMAT_RGB24,
		src->width, src->height, src->width * 4);
	assert(origin);
	
	double sx = (double)width / (double)src->width;
	double sy = (double)height / (double)src->height;
	
	cairo_surface_t * resized = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
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
	return 0;
}


static ssize_t darknet_predict(darknet_context_t * darknet, const bgra_image_t frame[1], ai_detection_t ** p_results)
{
	darknet_private_t * priv = darknet->priv;
	network * net = priv->net;

	int width = net->w;
	int height = net->h;
	debug_printf("network size: %d x %d\n", width, height);
	debug_printf("resize: %d x %d   --> %d x %d\n", frame->width, frame->height, width, height);
	bgra_image_t resized[1];
	memset(resized, 0, sizeof(resized));
	
	int rc = bgra_image_resize(resized, width, height, frame);
	assert(0 == rc);
	
	assert(resized->width == width && resized->height == height && resized->data);
	
	float * input = malloc(width * height * 3 * sizeof(float));
	bgra_image_to_f32(resized, input);
	
	network_predict(net, input);
	bgra_image_clear(resized);
	if(input) free(input);
	
	int count = 0;
	float thresh = 0.5f;
	float hier = 0.5f;
	float nms = 0.45;
	
	layer l = net->layers[net->n - 1];

	int relative = 0;		// relative coordinations
	double scale_x = 1;
	double scale_y = 1;
	if(!relative)
	{
		assert(frame->width > 0 && frame->height > 0);
		scale_x = 1.0 / (double)width;
		scale_y = 1.0 / (double)height;
	}
		
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
				result->x = (b.x - b.w / 2.0) * scale_x;
				result->y = (b.y - b.h / 2.0) * scale_y;
				result->cx = b.w * scale_x;
				result->cy = b.h * scale_y;
				
				debug_printf("result: bbox:{%.3f, %.3f, %.3f, %.3f}\n", 
					result->x, result->y, result->cx, result->cy);
				
				strncpy(result->class_names, label, sizeof(result->class_names));
				result->confidence = confidence;
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
