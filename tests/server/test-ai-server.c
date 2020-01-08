/*
 * test-ai-server.c
 * 
 * Copyright 2020 chehw <htc.chehw@gmail.com>
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
#include <json-c/json.h>

#include "utils.h"
#include "io-input.h"
#include "ai-engine.h"
#include "ann-plugin.h"

void ann_plugins_dump(ann_plugins_helpler_t * helpler)
{
	assert(helpler);
	printf("==== %s(%p):: num_plugins=%ld\n", __FUNCTION__, helpler, (long)helpler->num_plugins);
	ann_plugin_t ** plugins = helpler->plugins;
	for(int i = 0; i < helpler->num_plugins; ++i)
	{
		ann_plugin_t * plugin = plugins[i];
		assert(plugin);

		printf("%.3d: type=%s, filename=%s\n", i, plugin->type, plugin->filename); 
	}
	return;
}

#include <gtk/gtk.h>
#include <gst/gst.h>

typedef struct ai_conext
{
	io_input_t * input;
	ai_engine_t * engine;
	
	GtkWidget * window;
	GtkWidget * da;
	GtkWidget * header_bar;
	
	guint timer_id;
	cairo_surface_t * surface;
	int image_width;
	int image_height;
	
	bgra_image_t frame[1];
	long frame_number;
	
	int quit;
}ai_conext_t;

void ai_conext_free(ai_conext_t * ctx)
{
	if(NULL == ctx) return;
	ctx->quit = 1;
	if(ctx->input) io_input_cleanup(ctx->input);
	if(ctx->engine) ai_engine_cleanup(ctx->engine);
	ctx->input = NULL;
	ctx->engine = NULL;
	
	free(ctx);
	return;
}

static int shell_init(ai_conext_t * ctx);
ai_conext_t * ai_conext_new(int argc, char ** argv)
{
	int rc = 0;
	ai_conext_t * ctx = calloc(1, sizeof(*ctx));
	assert(ctx);
	
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, "plugins", NULL);
	assert(helpler);
	ann_plugins_dump(helpler);
	
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	
	const char * uri = "rtsp://admin:admin00000@192.168.1.107/Streaming/Channels/101";
	
	const char * input_type = "io-plugin::input-source";	// default
	json_object * jinput = json_object_new_object();
	json_object_object_add(jinput, "uri", json_object_new_string(uri));
    if(argc > 1)
    {
        if(strcasecmp(argv[1], "httpd") == 0 )
        {
            input_type = "io-plugin::httpd";
            json_object_object_add(jinput, "port", json_object_new_string("9002"));
        }
    }	
	io_input_t * input = io_input_init(NULL, input_type, ctx);
	rc = input->init(input, jinput);
	assert(0 == rc);
	
	json_object * jengine = json_object_new_object();
	json_object_object_add(jengine, "conf_file", json_object_new_string("models/yolov3.cfg"));
	json_object_object_add(jengine, "weights_file", json_object_new_string("models/yolov3.weights"));
	
	ai_engine_t * engine = ai_engine_init(NULL, "ai-engine::darknet", ctx);
	assert(engine);
	rc = engine->init(engine, jengine);
	assert(0 == rc);
	
	ctx->input = input;
	ctx->engine = engine;
	
	shell_init(ctx);
	return ctx;
}

static gboolean on_ai_context_update_frame(ai_conext_t * ctx);
int ai_conext_run(ai_conext_t * ctx)
{
	io_input_t * input = ctx->input;
	ai_engine_t * engine = ctx->engine;
	assert(input && engine);
	
	input->run(input);
	
//	GMainLoop * loop = g_main_loop_new(NULL, FALSE);
//	assert(loop);
	
	ctx->timer_id = g_timeout_add(200, (GSourceFunc)on_ai_context_update_frame, ctx);
	
	gtk_main();
	return 0;
}

int main(int argc, char **argv)
{
	int rc = 0;
	ai_conext_t * ctx = ai_conext_new(argc, argv);
	assert(ctx);

	rc = ai_conext_run(ctx);
	assert(0 == rc);
	
	ai_conext_free(ctx);
	return 0;
}

void draw_results(ai_conext_t * ctx, const input_frame_t * frame, json_object * jresult);
static gboolean on_ai_context_update_frame(ai_conext_t * ctx)
{
	if(ctx->quit)
	{
		ctx->timer_id = 0;
		return G_SOURCE_REMOVE;
	}
	
	int rc = 0;
	io_input_t * input = ctx->input;
	ai_engine_t * engine = ctx->engine;
	assert(input && engine);
	
	input_frame_t * frame = input_frame_new();
	long frame_number = input->get_frame(input, 0, frame);
	if(frame_number > ctx->frame_number && frame->data && frame->width > 0 && frame->height > 0)
	{
		char title[100] = "";
		snprintf(title, sizeof(title), "frame_number: %ld", (long)frame_number);
		gtk_header_bar_set_subtitle(GTK_HEADER_BAR(ctx->header_bar), title);
		ctx->frame_number = frame_number;
		json_object * jresult = NULL;
		
		app_timer_t timer[1];
		double time_elapsed = 0;
		
		app_timer_start(timer);
		rc = engine->predict(engine, frame, &jresult);
		time_elapsed = app_timer_stop(timer);
		printf("predict = %d, jresult = %p, time_elpased=%.3f ms\n", 
			rc, jresult, time_elapsed * 1000);
		if(0 == rc && jresult)
		{
			printf("detections: %s\n", 
				json_object_to_json_string_ext(jresult, JSON_C_TO_STRING_PRETTY));
			
		}
		draw_results(ctx, frame, jresult);
		if(jresult) json_object_put(jresult);
		
		
	}
	input_frame_clear(frame);
	input_frame_free(frame);
	return G_SOURCE_CONTINUE;
}



static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, ai_conext_t * ctx)
{
	cairo_surface_t * surface = ctx->surface;
	int width = gtk_widget_get_allocated_width(da);
	int height = gtk_widget_get_allocated_height(da);
	if(surface && ctx->image_width > 0 && ctx->image_height > 0)
	{
		double sx = (double)width / (double)ctx->image_width;
		double sy = (double)height / (double)ctx->image_height;
		cairo_save(cr);
		
		cairo_scale(cr, sx, sy);
		cairo_set_source_surface(cr, surface, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}else
	{
		cairo_set_source_rgb(cr, 0.2, 0.3, 0.4);
		cairo_paint(cr);
	}
	return FALSE;
}

static void shell_stop(ai_conext_t * ctx)
{
	ctx->quit = 1;
	if(ctx->timer_id) g_source_remove(ctx->timer_id);
	ctx->timer_id = 0;
	gtk_main_quit();
	return;
}

static int shell_init(ai_conext_t * ctx)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * grid = gtk_grid_new();
	GtkWidget * header_bar = gtk_header_bar_new();
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "Test AI-server::darknet");
	
	gtk_container_add(GTK_CONTAINER(window), grid);
	GtkWidget * frame = gtk_frame_new(NULL);
	GtkWidget * da = gtk_drawing_area_new();
	gtk_container_add(GTK_CONTAINER(frame), da);
	
	g_signal_connect(da, "draw", G_CALLBACK(on_da_draw), ctx);
	gtk_widget_set_hexpand(frame, TRUE);
	gtk_widget_set_vexpand(frame, TRUE);
	gtk_widget_set_size_request(frame, 640, 480);
	
	gtk_grid_attach(GTK_GRID(grid), frame, 0, 0, 1, 1);
	
	
	gtk_window_set_role(GTK_WINDOW(window), "debug::test");
	
	
	ctx->window = window;
	ctx->da = da;
	ctx->header_bar = header_bar;
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), ctx);
	gtk_widget_show_all(window);
	return 0;
}


void draw_results(ai_conext_t * ctx, const input_frame_t * frame, json_object * jresult)
{
	assert(frame);
	assert(frame->data && frame->width && frame->height);
	//~ assert(frame->type & input_frame_type_bgra);
	int width = frame->width;
	int height = frame->height;
	if(width < 0 || height < 0) return;
	
	bgra_image_t * bgra = (bgra_image_t *)frame->bgra;
	if(!(frame->type & input_frame_type_bgra))
	{
		bgra = bgra_image_init(NULL, width, height, NULL);
		assert(frame->length > 0);
		int rc = bgra_image_load_data(bgra, frame->data, frame->length);
		assert(0 == rc);
		
		assert(bgra->data && bgra->width == frame->width && bgra->height == frame->height);
	}

	cairo_surface_t * surface = ctx->surface;
	
	if(NULL == surface || width != ctx->image_width || height != ctx->image_height)
	{
		if(surface) cairo_surface_destroy(surface);
		ctx->surface = NULL;
		
		bgra_image_init(ctx->frame, width, height, bgra->data);
		
		assert(ctx->frame->data && ctx->frame->width == width && ctx->frame->height == height);
		surface = cairo_image_surface_create_for_data(ctx->frame->data, 
			CAIRO_FORMAT_RGB24, 
			width, height, 
			width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		ctx->surface = surface;
		ctx->image_width = width;
		ctx->image_height = height;
	}else
	{
		assert(surface);
		assert(ctx->frame->data && ctx->frame->width == width && ctx->frame->height == height);
		memcpy(ctx->frame->data, bgra->data, (frame->width * frame->height * 4));
		
	}
	
	if(jresult)
	{
		cairo_t * cr = cairo_create(surface);
		cairo_set_line_width(cr, 3);
		cairo_select_font_face(cr, "Droid Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, height / 36);
		
		json_object * jdetections = NULL;
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		if(ok && jdetections)
		{
			int count = json_object_array_length(jdetections);
			for(int i = 0; i < count; ++i)
			{
				json_object * jdet = json_object_array_get_idx(jdetections, i);
				const char * class_name = json_get_value(jdet, string, class);
				double confidence = json_get_value(jdet, double, confidence);
				double x = json_get_value(jdet, double, left) * (double)width;
				double y = json_get_value(jdet, double, top) * (double)height;
				double cx = json_get_value(jdet, double, width) * (double)width;
				double cy = json_get_value(jdet, double, height) * (double)height;
				
				cairo_rectangle(cr, x, y, cx, cy);
				cairo_set_source_rgb(cr, 1, 1, 0);
				cairo_stroke(cr);
				
				char text[200] = "";
				snprintf(text, sizeof(text), "%s - (%.2f%%)", class_name, confidence * 100);
				cairo_move_to(cr, x, y);
				cairo_set_source_rgb(cr, 0, 0, 1);
				cairo_show_text(cr, text);
			}
		}
		cairo_destroy(cr);
		
	}
	
	if(bgra != frame->bgra)
	{
		bgra_image_clear(bgra);
		free(bgra);
	}
	cairo_surface_mark_dirty(surface);
	gtk_widget_queue_draw(ctx->da);
	return;
	
}
