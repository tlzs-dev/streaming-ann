/*
 * demo-01.c
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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <pthread.h>

#include <curl/curl.h>

#include "ann-plugin.h"
#include "io-input.h"
#include "input-frame.h"
#include "ai-engine.h"

#include "da_panel.h"

#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
#define IO_PLUGIN_HTTPD "io-plugin::httpd"
#define IO_PLUGIN_HTTP_CLIENT   "io-plugin::httpclient"


static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;
#define global_lock() 	pthread_mutex_lock(&g_mutex)
#define global_unlock()	pthread_mutex_unlock(&g_mutex)

struct ai_context;
typedef struct shell_context
{
	void * user_data;
	GtkWidget * window;
	GtkWidget * header_bar;
	da_panel_t * panels[1];
	
	guint timer_id;
	int quit;
	
	input_frame_t frame[1];
	int is_dirty;
}shell_context_t;

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data);
void shell_context_cleanup(shell_context_t * shell);
int shell_run(shell_context_t * shell);

void shell_set_frame(shell_context_t * shell, const input_frame_t * frame)
{
	global_lock();
	shell->is_dirty = 1;
	input_frame_copy(shell->frame, frame);
	global_unlock();
	return;
}

typedef struct ai_context
{
	io_input_t * input;
	ai_engine_t * engine;
	
	shell_context_t * shell;
}ai_context_t;

static int on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	ai_context_t * ctx = input->user_data;
	assert(ctx && ctx->shell);

	shell_set_frame(ctx->shell, frame);
	return 0;
}



static ai_context_t g_ai_context[1];
int main(int argc, char **argv)
{
	ai_context_t * ctx = g_ai_context;
	shell_context_t * shell = shell_context_init(argc, argv, ctx);
	
	const char * uri = "/dev/video2";
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	io_input_t * input = io_input_init(NULL, IO_PLUGIN_DEFAULT, ctx);
	ctx->input = input;
	
	json_object * jconfig = json_object_new_object();
	assert(jconfig);
	
	json_object_object_add(jconfig, "name", json_object_new_string("input1"));
	json_object_object_add(jconfig, "uri", json_object_new_string(uri));
	input->init(input, jconfig);
	input->on_new_frame = on_new_frame;
	input->run(input);
	
	shell_run(shell);
	shell->quit = 1;
	
	io_input_cleanup(input);
	shell_context_cleanup(shell);
	
	return 0;
}




static shell_context_t g_shell[1];
static void init_windows(shell_context_t * shell);
shell_context_t * shell_context_init(int argc, char ** argv, void * user_data)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	
	ai_context_t * ctx = user_data;
	shell_context_t * shell = g_shell;
	shell->user_data = user_data;
	shell->quit = 0;
	
	assert(ctx);
	ctx->shell = shell;
	
	init_windows(shell);
	return shell;
}

static gboolean on_timeout(shell_context_t * shell);
int shell_run(shell_context_t * shell)
{
	shell->timer_id = g_timeout_add(100, (GSourceFunc)on_timeout, shell);
	gtk_main();
	return 0;
}

int shell_stop(shell_context_t * shell)
{
	if(shell->timer_id)
	{
		g_source_remove(shell->timer_id);
		shell->timer_id = 0;
	}
	gtk_main_quit();
	return 0;
}

void shell_context_cleanup(shell_context_t * shell)
{
	if(shell && !shell->quit)
	{
		shell->quit = 1;
		shell_stop(shell);
	}
	return;
}

static void init_windows(shell_context_t * shell)
{
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), vbox);
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "DEMO-01");

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_box_pack_start(GTK_BOX(vbox), panel->frame, TRUE, TRUE, 0);
	gtk_widget_set_size_request(panel->frame, 640, 480);
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	return;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame);
static gboolean on_timeout(shell_context_t * shell)
{
	if(shell->quit) return G_SOURCE_REMOVE;
	global_lock();
	if(!shell->is_dirty) {
		global_unlock();
		return G_SOURCE_CONTINUE;
	}
	
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	
	input_frame_copy(frame, shell->frame);
	shell->is_dirty = FALSE;
	global_unlock();
	
	
	draw_frame(shell->panels[0], frame);
	return G_SOURCE_CONTINUE;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame)
{
	assert(frame->width > 1 && frame->height > 1);
	cairo_surface_t * surface = panel->surface;
	if(NULL == panel->surface 
		|| panel->image_width != frame->width || panel->image_height != frame->height)
	{
		panel->surface = NULL;
		if(surface) cairo_surface_destroy(surface);
		
		unsigned char * data = realloc(panel->image_data, frame->width * frame->height * 4);
		assert(data);
		
		panel->image_data = data;
		panel->width = frame->width;
		panel->image_height = frame->height;
		
		surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32,
			frame->width, frame->height, 
			frame->width * 4);
		assert(surface && cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS);
		
		panel->surface = surface;
	}
	
	memcpy(panel->image_data, frame->data, frame->width * frame->height * 4);
	cairo_surface_mark_dirty(surface);
	gtk_widget_queue_draw(panel->da);
	return;
}
