/*
 * da_panel.c
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


/**
 * utils: draw summary graph
 * 
 * compile: (STAND_ALONE)
 * $ gcc -std=gnu99 -g -Wall -o test_da_panel da_panel.c -D_TEST_DA_PANEL -D_STAND_ALONE -lm -lpthread `pkg-config --cflags --libs gtk+-3.0`
 * 
*/
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <math.h>
#include "da_panel.h"

#include <gtk/gtk.h>

static void on_da_resize(GtkWidget * da, GdkRectangle * allocation, struct da_panel * panel);
static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, struct da_panel * panel);

static gboolean on_key_press(GtkWidget * da, GdkEventKey * event, struct da_panel * panel);
static gboolean on_key_release(GtkWidget * da, GdkEventKey * event, struct da_panel * panel);
static gboolean on_button_press(GtkWidget * da, GdkEventButton * event, struct da_panel * panel);
static gboolean on_button_release(GtkWidget * da, GdkEventButton * event, struct da_panel * panel);
static gboolean on_motion_notify(GtkWidget * da, GdkEventMotion * event, struct da_panel * panel);


static void clear_surface(struct da_panel * panel)
{
	assert(panel);
	cairo_surface_t * surface = panel->surface;
	if(NULL == surface) return;
	
	cairo_t * cr = cairo_create(surface);
	assert(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_paint(cr);
	cairo_destroy(cr);
	return;
}

struct da_panel * da_panel_init(struct da_panel * panel, int image_width, int image_height, void * shell)
{
	if(NULL == panel) panel = calloc(1, sizeof(*panel));
	assert(panel);
	panel->shell = shell;
	panel->clear = clear_surface;
	
	assert(image_width > 1 && image_height > 1);
	unsigned char * image_data = malloc(image_width * image_height * 4);	// bgra_data
	assert(image_data);
	
	cairo_surface_t * surface = cairo_image_surface_create_for_data(image_data, 
		CAIRO_FORMAT_ARGB32,
		image_width, image_height, 
		image_width * 4);
	assert(surface && CAIRO_STATUS_SUCCESS == cairo_surface_status(surface));
	
	
	
	panel->surface = surface;
	panel->image_data = image_data;
	panel->image_width = image_width;
	panel->image_height = image_height;
	
	panel->clear(panel);
	
	GtkWidget * da = gtk_drawing_area_new();
	GtkWidget * frame = gtk_frame_new(NULL);
	

	GtkWidget * scrollbar = gtk_scrollbar_new(GTK_ORIENTATION_VERTICAL, NULL);
	GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(hbox), da, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
	
	gtk_widget_set_size_request(frame, 600, 200 + 30);
	gtk_widget_set_hexpand(da, TRUE);
	gtk_widget_set_vexpand(da, TRUE);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	
	panel->frame = frame;
	panel->da = da;
	
	gint events = gtk_widget_get_events(da);
	gtk_widget_set_events(da, events 
		| GDK_KEY_PRESS_MASK
		| GDK_KEY_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_POINTER_MOTION_HINT_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK);
	
	
	
	g_signal_connect(da, "size-allocate", G_CALLBACK(on_da_resize), panel);
	g_signal_connect(da, "draw", G_CALLBACK(on_da_draw), panel);
	g_signal_connect(da, "key-press-event", G_CALLBACK(on_key_press), panel);
	g_signal_connect(da, "key-release-event", G_CALLBACK(on_key_release), panel);
	g_signal_connect(da, "button-press-event", G_CALLBACK(on_button_press), panel);
	g_signal_connect(da, "button-release-event", G_CALLBACK(on_button_release), panel);
	g_signal_connect(da, "motion-notify-event", G_CALLBACK(on_motion_notify), panel);

	return panel;
}
void da_panel_cleanup(struct da_panel * panel)
{
	if(NULL == panel) free(panel);
	if(panel->image_data)
	{
		if(panel->surface) cairo_surface_destroy(panel->surface);
		free(panel->image_data);
	}
	panel->surface = NULL;
	panel->image_data = NULL;
	panel->image_width = 0;
	panel->image_height = 0;
	return;
}

static void on_da_resize(GtkWidget * da, GdkRectangle * allocation, struct da_panel * panel)
{
	panel->width = allocation->width;
	panel->height = allocation->height;
	if(panel->width > 1 && panel->height > 1) gtk_widget_queue_draw(da);
	printf("size: %d x %d\n", panel->width, panel->height);
	return;
}

static gboolean on_da_draw(GtkWidget * da, cairo_t * cr, struct da_panel * panel)
{
	if(panel->width < 1|| panel->height < 1) return FALSE;
	if(NULL == panel->surface 
		|| panel->image_width < 1 || panel->image_height < 1)
	{
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_paint(cr);
		return FALSE;
	}
	
	double sx = (double)panel->width / (double)panel->image_width;
	double sy = (double)panel->height / (double)panel->image_height;
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, panel->surface, panel->x_offset, panel->y_offset);
	cairo_paint(cr);
	
	return FALSE;
}
	

static gboolean on_key_press(GtkWidget * da, GdkEventKey * event, struct da_panel * panel)
{
	return FALSE;
}

static gboolean on_key_release(GtkWidget * da, GdkEventKey * event, struct da_panel * panel)
{
	return FALSE;
}

static gboolean on_button_press(GtkWidget * da, GdkEventButton * event, struct da_panel * panel){
	return FALSE;
}

static gboolean on_button_release(GtkWidget * da, GdkEventButton * event, struct da_panel * panel){
	return FALSE;
}

static gboolean on_motion_notify(GtkWidget * da, GdkEventMotion * event, struct da_panel * panel){
	return FALSE;
}


#if defined(_TEST_DA_PANEL) && defined(_STAND_ALONE)

#define MAX_HEIGHT 100
#define NUM_CHAINS 3
static void draw_summary(struct da_panel * panel)
{
	//~ int indices[MAX_HEIGHT] = { 0 };
	//~ for(int i = 0; i < MAX_HEIGHT; ++i) indices[i] = i;
	
	// init dataset
	int indices_selected[MAX_HEIGHT] = { 0 };
	struct
	{
		int count;
		int indices[MAX_HEIGHT];
	}chains[NUM_CHAINS];
	
	memset(chains, 0, sizeof(chains));
	chains[0].count = 5;
	for(int i = 0; i < chains[0].count; ++i) {
		chains[0].indices[i] = 10 + i;
		indices_selected[chains[0].indices[i]] = 1;
	}
	
	chains[1].count = 3;
	for(int i = 0; i < chains[1].count; ++i) {
		chains[1].indices[i] = 30 + i;
		indices_selected[chains[1].indices[i]] = 1;
	}
	
	chains[2].count = 10;
	for(int i = 0; i < chains[2].count; ++i) {
		chains[2].indices[i] = 50 + i;
		indices_selected[chains[2].indices[i]] = 1;
	}
	
	cairo_t * cr = cairo_create(panel->surface);
	cairo_set_line_width(cr, 2);
	
	// draw main_chain
	int x = 0;
	int y = 0; 
	int item_size = 10;
	
	struct {
		double r, g, b, a;
	}colors[] = {
		[0] = {0, 1, 0, 1}, // green, means 'not selected'
		[1] = {1, 1, 0, 1}, // yellow, means 'selected'
		[2] = {1, 1, 1, 1}, // white, border color
	};
	
	for(int i = 0; i < MAX_HEIGHT; ++i)
	{
		x = i * item_size;
		int state = indices_selected[i];
		cairo_set_source_rgba(cr, colors[state].r,  colors[state].g,  colors[state].b,  colors[state].a);
		cairo_rectangle(cr, x, y, item_size, item_size);
		cairo_fill_preserve(cr);
		
		cairo_set_source_rgba(cr, colors[2].r,  colors[2].g,  colors[2].b,  colors[2].a);
		cairo_stroke(cr);
	}
	
	cairo_set_source_rgba(cr, 0, 1, 1, 0.8); // cyan for chain's nodes
	int radius = item_size / 2 - 2;
	if(radius < 1) radius = 1;
	
	for(int i = 0; i < NUM_CHAINS; ++i)
	{
		y += item_size * 2;
		for(int ii = 0; ii < chains[i].count; ++ ii)
		{ 
			x = chains[i].indices[ii] * item_size;
			cairo_arc(cr, x + radius, y + radius, radius, 0, M_PI * 2.0);
		}
		cairo_stroke(cr);
	}
	
	cairo_destroy(cr);
	return;
}
#undef NUM_CHAINS
#undef MAX_HEIGHT

int main(int argc, char ** argv)
{
	gtk_init(&argc, &argv);
	
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * grid = gtk_grid_new();
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	struct da_panel * panel = da_panel_init(NULL, 1000, 200, NULL);
	assert(panel);
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 0, 1, 1);
	gtk_widget_set_size_request(panel->frame, 640, 480);
	
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	
	
	
	gtk_widget_show_all(window);
	
	draw_summary(panel);
	gtk_widget_queue_draw(panel->da);
	
	gtk_main();
	
// cleanup
	da_panel_cleanup(panel);
	free(panel);
	
	return 0;
}
#endif

