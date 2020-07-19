/*
 * demo-03.c
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

#include <math.h>

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <pthread.h>

#include <json-c/json.h>
#include <curl/curl.h>

#include "ann-plugin.h"
#include "io-input.h"
#include "input-frame.h"

#include "utils.h"
#include "da_panel.h"

#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
#define IO_PLUGIN_HTTPD "io-plugin::httpd"
#define IO_PLUGIN_HTTP_CLIENT   "io-plugin::httpclient"

#define APP_TITLE "demo-03"

struct ai_context;

#define MAX_REGION_POINTS (16)
#define MAX_REGIONS (256)

typedef struct rect_d
{
	double x, y, cx, cy;
}rect_d;

typedef struct point_d
{
	double x, y;
}point_d;

typedef struct region_data
{
	ssize_t count;
	point_d points[MAX_REGION_POINTS];
	int state;	// -1: unknown, 0: empty, 1: be occupied.
	rect_d bbox;
}region_data_t;

typedef struct shell_context
{
	void * user_data;
	GtkWidget * window;
	GtkWidget * header_bar;
	da_panel_t * panels[2];
	
	guint timer_id;
	int quit;
	
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	pthread_t th;
	
	input_frame_t frame[1];
	int is_busy;
	int is_dirty;
	
	CURL * curl;
	json_tokener * jtok;
	json_object * jresult;
	enum json_tokener_error jerr;
	
	
}shell_context_t;

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data);
void shell_context_cleanup(shell_context_t * shell);
int shell_run(shell_context_t * shell);

static inline void shell_set_frame(shell_context_t * shell, const input_frame_t * frame)
{
	input_frame_copy(shell->frame, frame);
	return;
}


typedef struct ai_context
{
	io_input_t * input;
	shell_context_t * shell;
	
	const char * app_title;
	const char * conf_file;
	const char * server_url;
	const char * video_src;
	
	const char * css_file;
	
	json_object * jconfig;
	json_object * jinput;
	json_object * jai_engine;
	
	// settings
	ssize_t regions_count;
	region_data_t * regions;
	
	int image_width;
	int image_height;
	cairo_surface_t * masks;
	
}ai_context_t;

static int on_new_frame(io_input_t * input, const input_frame_t * frame)
{
	shell_context_t * shell = input->user_data;
	assert(shell);
	
	pthread_mutex_lock(&shell->mutex);
	if(!shell->is_busy) {
		shell->is_busy = 1;
		shell_set_frame(shell, frame);
		pthread_cond_signal(&shell->cond);
	}
	pthread_mutex_unlock(&shell->mutex);
	return 0;
}

static ai_context_t g_ai_context[1] = {{
	.app_title = APP_TITLE,
	.conf_file = "demo-03.json",
	//~ .server_url = "http://127.0.0.1:9090/ai",
	//~ .video_src = "/dev/video1",
}};
#include <getopt.h>

static void print_usuage(int argc, char ** argv)
{
	printf("Usuage: %s [--server_url=<url>] [--video_src=<rtsp://camera_ip>] \n", argv[0]);
	return;
}

static int load_config(ai_context_t * ctx, const char * conf_file);
int parse_args(int argc, char ** argv, ai_context_t * ctx)
{
	static struct option options[] = {
		{"conf_file", required_argument, 0, 'c' },	// config_file
		{"server_url", required_argument, 0, 's' },	// AI server URL
		{"video_src", required_argument, 0, 'v' },	// camera(local/rtsp/http) or video file
		{"help", no_argument, 0, 'h' },
		{NULL, 0, 0, 0 },
	};
	
	const char * conf_file = ctx->conf_file;
	const char * server_url = NULL;
	const char * video_src = NULL;
	
	while(1)
	{
		int index = 0;
		int c = getopt_long(argc, argv, "c:s:v:h", options, &index);
		if(c < 0) break;
		switch(c)
		{
		case 'c': conf_file = optarg; break;
		case 's': server_url = optarg; break;
		case 'v': video_src = optarg; break;
		case 'h': 
		default:
			print_usuage(argc, argv); exit(0);
		}
	}
	
	ctx->conf_file = conf_file;
	ctx->server_url = server_url;
	ctx->video_src = video_src;
	
	assert(ctx->conf_file && ctx->conf_file[0]);
	
	int rc = load_config(ctx, conf_file);
	return rc;
}

int main(int argc, char **argv)
{
	ai_context_t * ctx = g_ai_context;
	int rc = parse_args(argc, argv, ctx);
	assert(0 == rc && ctx->jinput && ctx->server_url && ctx->server_url[0]);
	
	shell_context_t * shell = shell_context_init(argc, argv, ctx);
	ann_plugins_helpler_init(NULL, "plugins", NULL);
	
	io_input_t * input = io_input_init(NULL, IO_PLUGIN_DEFAULT, shell);
	ctx->input = input;
	
	input->init(input, ctx->jinput);
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


static size_t on_response(void * ptr, size_t size, size_t n, void * user_data)
{
	assert(user_data);
	shell_context_t * shell = user_data;
	
	json_tokener * jtok = shell->jtok;
	assert(jtok);
	
	size_t cb = size * n;
	if(0 == cb) {
		json_tokener_reset(jtok);
		return 0;
	}
	
	enum json_tokener_error jerr = json_tokener_error_parse_eof;
	json_object * jresult = json_tokener_parse_ex(jtok, (char *)ptr, cb);
	jerr = json_tokener_get_error(jtok);
	
	if(jerr == json_tokener_continue) return cb;
	
	shell->jerr = jerr;
	if(jerr == json_tokener_success) 
	{
		pthread_mutex_lock(&shell->mutex);
		if(shell->jresult) json_object_put(shell->jresult);
		shell->jresult = jresult;
		pthread_mutex_unlock(&shell->mutex);
	}
	json_tokener_reset(jtok);
	return cb;
}

#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p) {
		free(p);
		*(void **)ptr = NULL;
	}
	return;
}


int ai_request(shell_context_t * shell, const input_frame_t * frame)
{
	ai_context_t * ctx = shell->user_data;
	assert(ctx);
	
	CURL * curl = shell->curl;
	assert(curl);
	
	AUTO_FREE_PTR unsigned char * jpeg_data = NULL;
	long cb_jpeg = bgra_image_to_jpeg_stream((bgra_image_t *)frame->bgra, &jpeg_data, 95);
	assert(cb_jpeg > 0 && jpeg_data);
	curl_easy_reset(curl);
	curl_easy_setopt(curl, CURLOPT_URL, ctx->server_url);
	
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, cb_jpeg);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jpeg_data);
	
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, shell);
	
	struct curl_slist * headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: image/jpeg");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	
	CURLcode ret = 0;
	ret = curl_easy_perform(curl);
	curl_slist_free_all(headers);
	
	if(ret != CURLE_OK) {
		fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(ret));
		return -1;
	}
	
	return 0;
}

static gboolean on_idle(shell_context_t * shell);
void * ai_thread(void * user_data)
{
	shell_context_t * shell = user_data;
	int rc = 0;
	pthread_mutex_lock(&shell->mutex);
	while(!shell->quit)
	{
		rc = pthread_cond_wait(&shell->cond, &shell->mutex);
		if(rc || shell->quit) break;
		shell->is_busy = 1;
		pthread_mutex_unlock(&shell->mutex);
		
		rc = ai_request(shell, shell->frame);
		
		pthread_mutex_lock(&shell->mutex);
		//shell->is_busy = 0;
		shell->is_dirty = (0 == rc);
		g_idle_add((GSourceFunc)on_idle, shell);
		
	}
	
	pthread_mutex_unlock(&shell->mutex);
	pthread_exit((void *)(long)rc);
}

shell_context_t * shell_context_init(int argc, char ** argv, void * user_data)
{
	gst_init(&argc, &argv);
	gtk_init(&argc, &argv);
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	ai_context_t * ctx = user_data;
	shell_context_t * shell = g_shell;
	
	shell->user_data = user_data;
	shell->quit = 0;
	
	int rc = 0;
	rc = pthread_mutex_init(&shell->mutex, NULL);
	if(0 == rc) rc = pthread_cond_init(&shell->cond, NULL);
	assert(0 == rc);
	
	rc = pthread_create(&shell->th, NULL, ai_thread, shell);
	assert(0 == rc);
	
	shell->curl = curl_easy_init();
	assert(shell->curl);
	shell->jtok = json_tokener_new();
	

	assert(ctx);
	ctx->shell = shell;
	
	init_windows(shell);
	return shell;
}


int shell_run(shell_context_t * shell)
{
//	shell->timer_id = g_timeout_add(200, (GSourceFunc)on_timeout, shell);
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
	
	if(shell->th)
	{
		pthread_cond_broadcast(&shell->cond);
		void * exit_code = NULL;
		int rc = pthread_join(shell->th, &exit_code);
		printf("ai_thread exited with code %ld, rc = %d\n", (long)exit_code, rc);
	}
	return;
}


static gboolean draw_main_panel(struct da_panel * panel, cairo_t * cr, void * user_data);
static gboolean draw_masks_panel(struct da_panel * panel, cairo_t * cr, void * user_data);
static void init_windows(shell_context_t * shell)
{
	ai_context_t * ctx = shell->user_data;
	assert(ctx);
	
	GtkWidget * window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget * header_bar = gtk_header_bar_new();
	GtkWidget * grid = gtk_grid_new();
	
	gtk_window_set_titlebar(GTK_WINDOW(window), header_bar);
	gtk_container_add(GTK_CONTAINER(window), grid);
	
	if(ctx->css_file) {
		GError * gerr = NULL;
		GtkCssProvider * css = gtk_css_provider_new();
		gboolean ok = gtk_css_provider_load_from_path(css, ctx->css_file, &gerr);
		if(!ok || gerr) {
			fprintf(stderr, "gtk_css_provider_load_from_path(%s) failed: %s\n",
				ctx->css_file, 
				gerr?gerr->message:"unknown error");
			if(gerr) g_error_free(gerr);
		}else
		{
			GdkScreen* screen = gtk_window_get_screen(GTK_WINDOW(window));
			gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER);
		}
	}
	
	gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header_bar), TRUE);
	gtk_header_bar_set_title(GTK_HEADER_BAR(header_bar), "DEMO-01");

	struct da_panel * panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[0] = panel;
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 0, 1, 1);
	gtk_widget_set_size_request(panel->da, 640, 480);
	panel->on_draw = draw_main_panel;
	
	
	panel = da_panel_init(NULL, 640, 480, shell);
	assert(panel);
	shell->panels[1] = panel;
	gtk_grid_attach(GTK_GRID(grid), panel->frame, 0, 1, 1, 1);
	gtk_widget_set_size_request(panel->da, 640, 160);
	panel->on_draw = draw_masks_panel;
	
	g_signal_connect_swapped(window, "destroy", G_CALLBACK(shell_stop), shell);
	gtk_widget_show_all(window);
	
	
	return;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult);
static gboolean on_idle(shell_context_t * shell)
{
	if(shell->quit) return G_SOURCE_REMOVE;
	
	pthread_mutex_lock(&shell->mutex);
	if(!shell->frame->data) {
		pthread_mutex_unlock(&shell->mutex);
		return G_SOURCE_REMOVE;
	}
	
	
	json_object * jresult = NULL;
	input_frame_t frame[1];
	memset(frame, 0, sizeof(frame));
	input_frame_copy(frame, shell->frame);
	if(shell->jresult) jresult = json_object_get(shell->jresult);
	shell->is_dirty = 0;
	shell->is_busy = 0;
	pthread_mutex_unlock(&shell->mutex);
	
	
	draw_frame(shell->panels[0], frame, jresult);
	input_frame_clear(frame);
	if(jresult) json_object_put(jresult);
	return G_SOURCE_REMOVE;
}

static inline rect_d rect_d_intesect(const rect_d r1, const rect_d r2)
{
	if(r2.x > (r1.x + r1.cx) || (r2.x + r2.cx) < r1.x) return (rect_d){0,0,0,0};
	if(r2.y > (r1.y + r1.cy) || (r2.y + r2.cy) < r1.y) return (rect_d){0,0,0,0};
	
	double x1 = (r1.x > r2.x)?r1.x:r2.x;
	double y1 = (r1.y > r2.y)?r1.y:r2.y;
	double x2 = ((r1.x + r1.cx) < (r2.x + r2.cx))?(r1.x + r1.cx):(r2.x + r2.cx);
	double y2 = ((r1.y + r1.cy) < (r2.y + r2.cy))?(r1.y + r1.cy):(r2.y + r2.cy);
	
	return (rect_d){x1, y1,(x2 - x1),(y2 - y1)};
}

static inline int check_region_state(ai_context_t * ctx, double x, double y, double cx, double cy)
{
	region_data_t * regions = ctx->regions;
	cairo_surface_t * masks = ctx->masks;
	assert(masks && regions);
	
	unsigned char * masks_data = cairo_image_surface_get_data(masks);
	int width = cairo_image_surface_get_width(masks);
	int height = cairo_image_surface_get_height(masks);
	
	int center_x = (int)((x + cx / 2) * width);
	int bottom_y = (int)((y + cy) * height);
	
	if(center_x < 0 || center_x >= width) return -1;
	if(bottom_y < 0 || bottom_y >= height) return -1;
	
	unsigned char color_index = masks_data[(bottom_y * width + center_x) * 4];
	if(color_index < 0 || color_index > ctx->regions_count) return -1;
	--color_index;
	
	region_data_t * current = &regions[color_index];
	current->state = 1;
	
	
	//~ rect_d det = (rect_d){x, y, cx, cy};
	//~ rect_d intersect = rect_d_intesect(current->bbox, det);
	
//~ #define IOU_THRESHOLD 0.8
	//~ double intersect_area = intersect.cx * intersect.cy;
	//~ double current_area = current->bbox.cx * current->bbox.cy;
	//~ double det_area = det.cx * det.cy;
	
	//~ assert(intersect_area >= 0 && (current_area + det_area) > 0);
	//~ current->state = ((intersect_area / (current_area + det_area)) > IOU_THRESHOLD);
//~ #undef IOU_THRESHOLD
	return 0;
}

static void draw_frame(da_panel_t * panel, const input_frame_t * frame, json_object * jresult)
{
	shell_context_t * shell = panel->shell;
	assert(shell && shell->user_data);
	ai_context_t * ctx = shell->user_data;
		
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
	
	assert(ctx->regions);
	// init region masks
	if(NULL == ctx->masks || frame->width != ctx->image_width || frame->height != ctx->image_height) {
		if(ctx->masks) cairo_surface_destroy(ctx->masks);
		ctx->masks = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame->width, frame->height);
		ctx->image_width = frame->width;
		ctx->image_height = frame->height;
		assert(ctx->masks && cairo_surface_status(ctx->masks) == CAIRO_STATUS_SUCCESS);
		
		cairo_t * cr = cairo_create(ctx->masks);
		cairo_set_source_rgba(cr, 255, 255, 255, 1);
		cairo_paint(cr);
		
		//cairo_scale(cr, (double)frame->width, (double)frame->height);
		for(int i = 0; i < ctx->regions_count; ++i) {
			double color_index = (double)(i + 1) / 255.0;
			cairo_set_source_rgb(cr, color_index, color_index, color_index);
			region_data_t * region = &ctx->regions[i];
			if(region->count >= 3) {
				cairo_new_path(cr);
				cairo_move_to(cr, region->points[0].x * frame->width, region->points[0].y * frame->height);
				for(int ii = 1; ii < region->count; ++ii) {
					cairo_line_to(cr, region->points[ii].x * frame->width, region->points[ii].y * frame->height);
				}
				cairo_close_path(cr);
				cairo_fill_preserve(cr);
				cairo_stroke(cr);
			}
		}
		cairo_destroy(cr);
	}
	
	// reset parking space states
	for(int i = 0; i < ctx->regions_count; ++i) ctx->regions[i].state = 0;
	
	if(jresult)
	{
		json_object * jdetections = NULL;
		cairo_t * cr = cairo_create(surface);
		
		json_bool ok = json_object_object_get_ex(jresult, "detections", &jdetections);
		
		double width = frame->width;
		double height = frame->height;
		
		if(ok && jdetections)
		{
			int count = json_object_array_length(jdetections);
			cairo_set_line_width(cr, 2);
			cairo_set_source_rgb(cr, 1, 1, 0);
			cairo_select_font_face(cr, "IPAMincho", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cr, 15);
			
			for(int i = 0; i < count; ++i)
			{
				json_object * jdet = json_object_array_get_idx(jdetections, i);
				assert(jdet);
				const char * class_name = json_get_value(jdet, string, class);

				if(strcasecmp(class_name, "car") && strcasecmp(class_name, "truck")) continue;
							
				double x = json_get_value(jdet, double, left);
				double y = json_get_value(jdet, double, top);
				double cx = json_get_value(jdet, double, width);
				double cy = json_get_value(jdet, double, height);
				
				check_region_state(ctx, x, y, cx, cy);
				
				cairo_rectangle(cr, x * width, y * height, cx * width, cy * height);
				cairo_stroke(cr);
				
				cairo_arc(cr, (x + cx / 2) * width, (y + cy) * height, 5, 0.0, M_PI * 2.0);
				cairo_fill(cr);
				
				cairo_move_to(cr, x * width, y * height + 20);
				cairo_show_text(cr, class_name);
			}
		}
		
		cairo_destroy(cr);
	}
	
	gtk_widget_queue_draw(panel->da);
	gtk_widget_queue_draw(shell->panels[1]->da);
	return;
}

static int load_config(ai_context_t * ctx, const char * conf_file)
{
	json_object * jconfig = json_object_from_file(conf_file);
	assert(jconfig);
	ctx->jconfig = jconfig;
	ctx->app_title = json_get_value_default(jconfig, string, title, APP_TITLE); 
	ctx->css_file = json_get_value(jconfig, string, styles);
	
	json_object * jinput = NULL;
	json_object * jai_engine = NULL;
	json_bool ok = FALSE;
	
	ok = json_object_object_get_ex(jconfig, "input", &jinput);
	assert(ok && jinput);
	ctx->jinput = jinput;
	
	ok = json_object_object_get_ex(jconfig, "ai-engine", &jai_engine);
	assert(ok && jai_engine);
	
	if(NULL == ctx->video_src) ctx->video_src = json_get_value(jinput, string, uri);
	else json_object_object_add(jinput, "uri", json_object_new_string(ctx->video_src));
	
	if(NULL == ctx->video_src) {
		ctx->video_src = "/dev/video0";
		json_object_object_add(jinput, "uri", json_object_new_string(ctx->video_src));
	}
	assert(ctx->video_src && ctx->video_src[0]);
	
	
	if(NULL == ctx->server_url) ctx->server_url = json_get_value(jai_engine, string, url);
	else json_object_object_add(jai_engine, "url", json_object_new_string(ctx->server_url));
	
	if(NULL == ctx->server_url) {
		ctx->server_url =  "http://127.0.0.1:9090/ai";
		json_object_object_add(jai_engine, "url", json_object_new_string(ctx->server_url));
	}
	assert(ctx->server_url && ctx->server_url[0]);
	
	json_object * jsettings = NULL;
	ok = json_object_object_get_ex(jconfig, "settings", &jsettings);
	assert(ok && jsettings);
	
	int regions_count = json_object_array_length(jsettings);
	if(regions_count <= 0) return -1;
	
	assert(regions_count < MAX_REGIONS);
	
	ctx->regions_count = regions_count;
	if(NULL == ctx->regions) ctx->regions = calloc(MAX_REGIONS, sizeof(*ctx->regions));
	assert(ctx->regions);
	
	for(int i = 0; i < regions_count; ++i)
	{
		json_object * jpoints = json_object_array_get_idx(jsettings, i);
		assert(jpoints);
		int points_count = json_object_array_length(jpoints);
		if(points_count > MAX_REGION_POINTS) points_count = MAX_REGION_POINTS;
		
		region_data_t * region = &ctx->regions[i];
		region->count = points_count;

#define set_if_min_or_max(value, min, max) do { if(value > max) max = value; if(value < min) min = value; } while(0)

		double x_min = 99999, y_min = 99999;
		double x_max = -99999, y_max = -99999;
		for(int ii = 0; ii < points_count; ++ii) {
			json_object * jpoint = json_object_array_get_idx(jpoints, ii);
			region->points[ii].x = json_object_get_double(json_object_array_get_idx(jpoint, 0));
			region->points[ii].y = json_object_get_double(json_object_array_get_idx(jpoint, 1));
			
			set_if_min_or_max(region->points[ii].x, x_min, x_max);
			set_if_min_or_max(region->points[ii].y, y_min, y_max);
		}
		if(points_count > 0) {
			region->bbox.x = x_min;
			region->bbox.y = y_min;
			region->bbox.cx = (x_max - x_min);
			region->bbox.cy = (y_max - y_min);
		}
#undef set_if_min_or_max
	}
	return 0;
}


static gboolean draw_main_panel(struct da_panel * panel, cairo_t * cr, void * user_data)
{
	shell_context_t * shell = user_data;
	assert(shell);
	
	ai_context_t * ctx = shell->user_data;
	assert(ctx);
	
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
	cairo_save(cr);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, panel->surface, panel->x_offset, panel->y_offset);
	cairo_paint(cr);
	cairo_restore(cr);
	
	if(NULL == ctx->regions || ctx->regions_count <= 0) return FALSE;
	
	//debug_printf("masks: %d x %d\n", ctx->image_width, ctx->image_height);
	for(int i = 0; i < ctx->regions_count; ++i)
	{
		region_data_t * region = &ctx->regions[i];
		if(region->count <= 0) continue;
		
		double r = 1, g = 0, b = 0;
		if(region->state < 0) { r = 0.5; g = 0.5; b = 0.5; }
		else if(region->state == 0) { r = 0; g = 1; b = 0; }
		
		double dashes[1] = { 1 };
		cairo_set_line_width(cr, 2);
		cairo_set_dash(cr, dashes, 1, 0);
		cairo_set_source_rgba(cr, r, g, b, 0.3);
		cairo_new_path(cr);
		cairo_move_to(cr, region->points[0].x * panel->width, region->points[0].y * panel->height);
		for(int ii = 1; ii < region->count; ++ii) {
			cairo_line_to(cr, region->points[ii].x * panel->width, region->points[ii].y * panel->height);
		}
		cairo_close_path(cr);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, r, g, b, 1.0);
		cairo_stroke(cr);
	}
	
	return FALSE;
}

static gboolean draw_masks_panel(struct da_panel * panel, cairo_t * cr, void * user_data)
{
	shell_context_t * shell = user_data;
	assert(shell);
	
	ai_context_t * ctx = shell->user_data;
	assert(ctx);
	
	//debug_printf("masks: %d x %d", ctx->image_width, ctx->image_height);
	if(panel->width < 1|| panel->height < 1) return FALSE;
	
	if(NULL == ctx->masks 
		|| ctx->image_width < 1 || ctx->image_height < 1)
	{
		cairo_set_source_rgba(cr, 0, 0, 0, 1);
		cairo_paint(cr);
		return FALSE;
	}
	
	
	
	double sx = (double)panel->width / (double)ctx->image_width;
	double sy = (double)panel->height / (double)ctx->image_height;
	cairo_save(cr);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, ctx->masks, panel->x_offset, panel->y_offset);
	cairo_paint(cr);
	cairo_restore(cr);
	
	
	cairo_move_to(cr, 20, 30);
	cairo_set_source_rgb(cr, 0, 0, 1);
	cairo_set_font_size(cr, 18);
	cairo_set_line_width(cr, 3);
	cairo_select_font_face(cr, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_show_text(cr, "masks view");
	return FALSE;
	
}

