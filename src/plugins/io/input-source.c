/*
 * input-source.c
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

#include "input-source.h"
#include "fs-notify.h"

#include <pthread.h>
#include <gst/gst.h>

#include <glib.h>
#include <gio/gio.h>

#include "utils.h"

enum input_source_type guess_file_type(const char * path_name, int * subtype)
{
	gboolean uncertain = TRUE;
	gchar * content_type = NULL;
	enum input_source_type type = input_source_type_unknown;
	content_type = g_content_type_guess(path_name, NULL, 0, &uncertain);
	if(!uncertain)
	{
		type = input_source_type_file;
		if(strcasecmp(content_type, "image/jpeg") == 0)
		{
			*subtype = input_source_subtype_jpeg;
		}else if(strcasecmp(content_type, "image/png") == 0){
			*subtype = input_source_subtype_png;
		}else
		{
			if(strcasecmp(content_type, "video/mp4") == 0
				|| strcasecmp(content_type, "video/mpeg") == 0
				|| strcasecmp(content_type, "video/quicktime") == 0
				|| strcasecmp(content_type, "video/webm") == 0
				|| strcasecmp(content_type, "video/x-flv") == 0
				|| strcasecmp(content_type, "video/x-matroska") == 0
				|| strcasecmp(content_type, "video/x-msvideo") == 0
				|| strcasecmp(content_type, "video/x-ms-wmv") == 0
				|| 0)
			{
				*subtype = input_source_subtype_video;
			}else
			{
				type = input_source_type_unknown;
				*subtype = -1;
			}
		}
	}
	g_free(content_type);
	return type;
}

#define PROTOCOL_rtsp 		"rtsp://"
#define PROTOCOL_http 		"http://"
#define PROTOCOL_https 		"https://"
#define PROTOCOL_file		"file://"
#define PROTOCOL_v4l2		"/dev/video"
#define PROTOCOL_fsnotify	"fsnotify://"

#define is_type(uri, type) (strncasecmp(uri, PROTOCOL_##type, sizeof(PROTOCOL_##type) - 1) == 0)
#define break_if_is_type(ret, uri, type) if(is_type(uri, type)) { ret = input_source_type_##type; break; }

#define parse_uri_simple(uri, ret_type) do {				\
		break_if_is_type(ret_type, uri, rtsp);				\
		break_if_is_type(ret_type, uri, http);				\
		break_if_is_type(ret_type, uri, https);				\
		break_if_is_type(ret_type, uri, file);				\
		break_if_is_type(ret_type, uri, v4l2);				\
		break_if_is_type(ret_type, uri, fsnotify);			\
	} while(0)


enum input_source_type input_source_type_parse_uri(const char * uri, char ** p_cooked_uri, int * subtype)
{
	enum input_source_type type = input_source_type_unknown;
	parse_uri_simple(uri, type);
	
	*subtype = input_source_subtype_default;
	switch(type)
	{
	case input_source_type_rtsp:
	case input_source_type_v4l2:
	case input_source_type_fsnotify:
		*p_cooked_uri = strdup(uri);
		return type;
	case input_source_type_http:
	case input_source_type_https:
		{
			char * p_ext = strrchr(uri, '.');
			if(p_ext && strcasecmp(p_ext, ".m38u") == 0)
			{
				*subtype = input_source_subtype_hls;
			}
		}
		*p_cooked_uri = strdup(uri);
		return type;
	case input_source_type_file:
		uri += sizeof(PROTOCOL_file) - 1;
		if(uri[0])
		{
			guess_file_type(uri, subtype);
		}
		*p_cooked_uri = strdup(uri);
		return type;
	default:
		break;
	}
	
	if(uri[0] >= 0x30 && uri[0] <= 0x39 && uri[1] == '\0')
	{
		type = input_source_type_v4l2;
		char cooked_uri[100] = "";
		snprintf(cooked_uri, sizeof(cooked_uri), "/dev/video%d", (int)atoi(uri));
		*p_cooked_uri = strdup(cooked_uri);
		return type;
	}

	type = guess_file_type(uri, subtype);
	*p_cooked_uri = strdup(uri);
	return type;
}

typedef struct video_source
{
	/* base object */
	input_source_t * input;			// base object
	input_frame_t frame[1];
	
	/* private context */
	GMainLoop * loop;
	GstElement * pipeline;
	GstElement * filter;
	GstElement * sink;

	char * gst_command;

	/* virtual methods */
	//~ int (* play)(input_source_t * input);
	//~ int (* stop)(input_source_t * input);
	//~ int (* pause)(input_source_t * input);
}video_source_t;
static int video_source_set_uri(video_source_t * src, input_source_t * input, const char * cooked_uri, int subtype);
static int video_source_play(input_source_t * input);
static int video_source_stop(input_source_t * input);
static int video_source_pause(input_source_t * input);
static void video_source_cleanup(video_source_t * src);


typedef struct static_file_source
{
	/* base object */
	input_source_t * input;		// base object
	input_frame_t frame[1];

	/* private context */
	char path_name[PATH_MAX];
	
	/* virtual methods */
	//~ int (* play)(input_source_t * input);
	//~ int (* stop)(input_source_t * input);
	//~ int (* pause)(input_source_t * input);
}static_file_source_t;
static int static_file_source_set_uri(static_file_source_t * src, input_source_t * input, const char * cooked_uri, int subtype);
static int static_file_source_play(input_source_t * input);
static int static_file_source_stop(input_source_t * input);
static int static_file_source_pause(input_source_t * input);
static void static_file_source_cleanup(static_file_source_t * src);




typedef struct fs_move_event_data
{
	uint32_t cookie;
	fsnotify_data_t * data;
	struct fs_move_event_data * next;
}fs_move_event_data_t;

typedef struct fs_move_event_queue
{
	fs_move_event_data_t * head;
}fs_move_event_queue_t;

int fs_move_event_queue_enter(fs_move_event_queue_t * queue, uint32_t cookie, fsnotify_data_t * notify_data)
{
	assert(notify_data);
	debug_printf("%s()::cookie=%u, data=%p, name=%s", __FUNCTION__, cookie, notify_data, notify_data->path_name);
	assert(cookie != 0);
	
	fs_move_event_data_t * node = calloc(1, sizeof(*node));
	assert(node);
	
	node->cookie = cookie;
	node->data = notify_data;
	
	node->next = queue->head;
	queue->head = node;
	return 0;
}

fs_move_event_data_t * fs_move_event_queue_leave(fs_move_event_queue_t * queue, uint32_t cookie)
{
	debug_printf("%s()::cookie=%u", __FUNCTION__, cookie);
	fs_move_event_data_t * node = queue->head;
	fs_move_event_data_t * prev = NULL;
	while(node)
	{
		if(node->cookie == cookie)
		{
			if(NULL == prev) queue->head = node->next;
			else prev->next = node->next;
			node->next = NULL;
			return node;
		}
		prev = node;
		node = node->next;
	}
	return NULL;
}

void fs_move_event_data_free(fs_move_event_data_t * data)
{
	free(data);
}

typedef struct fsnotify_source
{
	/* base object */
	input_source_t * input;		// base object
	input_frame_t frame[1];

	/* private context */
	filesystem_notify_t fsnotify[1];
	
	int mode;				// 0: json + images; 	1: images only
	int auto_delete_file;
	char images_path[PATH_MAX];
	char json_path[PATH_MAX];
	
	fs_move_event_queue_t queue[1];

	/* virtual methods */
	//~ int (* play)(input_source_t * input);
	//~ int (* stop)(input_source_t * input);
	//~ int (* pause)(input_source_t * input);
}fsnotify_source_t;
static int fsnotify_source_set_uri(fsnotify_source_t * src, input_source_t * input, const char * cooked_uri, int subtype);
static int fsnotify_source_play(input_source_t * input);
static int fsnotify_source_stop(input_source_t * input);
static int fsnotify_source_pause(input_source_t * input);
static void fsnotify_source_cleanup(fsnotify_source_t * src);

typedef struct input_source_private
{
	union		// base object 
	{
		input_source_t * input;
		video_source_t video_src[1];
		static_file_source_t file_src[1];
		fsnotify_source_t fsnotify_src[1];
	};

	input_frame_t * frame_buffer[2];
	char * uri;
	char * cooked_uri;

	int is_running;
	int is_paused;

	long frame_number;
	int width;
	int height;
	
	pthread_t th;
	pthread_mutex_t mutex;

}input_source_private_t;
input_source_private_t * input_source_private_new(input_source_t * input)
{
	input_source_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);

	int rc = pthread_mutex_init(&priv->mutex, NULL);
	assert(0 == rc);

	priv->input = input;
	input->priv = priv;
	
	
	return priv;
}

void input_source_private_cleanup(input_source_private_t * priv)
{
	if(NULL == priv) return;
	input_source_t * input = priv->input;

	if(input->type != input_source_type_unknown)
	{
		switch(input->type)
		{
		case input_source_type_fsnotify: 
			fsnotify_source_cleanup(priv->fsnotify_src); 
			break;
		case input_source_type_file:
			if(input->subtype != input_source_subtype_video) {
				static_file_source_cleanup(priv->file_src);
				break;
			}
		default:
			video_source_cleanup(priv->video_src);
			break;
		}
	}
	
	if(priv->uri) free(priv->uri);
	if(priv->cooked_uri) free(priv->cooked_uri);
	pthread_mutex_destroy(&priv->mutex);
	free(priv);
	return;
}


static int	input_source_set_uri(struct input_source * input, const char * uri);
static int 	input_source_restart(struct input_source * input);
static int 	input_source_on_new_frame(struct input_source * input, const input_frame_t * frame);
static long input_source_set_frame(struct input_source * input, const input_frame_t * frame);
static long input_source_get_frame(struct input_source * input, long prev_frame, input_frame_t * frame);

input_source_t * input_source_new(void * user_data)
{
	input_source_t * input = calloc(1, sizeof(* input));
	assert(input);

	input->user_data = user_data;
	input->set_uri = input_source_set_uri;
	//~ input->play = input_source_play;
	//~ input->stop = input_source_stop;
	//~ input->pause = input_source_pause;
	input->restart = input_source_restart;
	input->on_new_frame = input_source_on_new_frame;
	input->set_frame = input_source_set_frame;
	input->get_frame = input_source_get_frame;

	input_source_private_t * priv = input_source_private_new(input);
	assert(priv && priv->input == input && input->priv == priv);

	return input;
}

void input_source_cleanup(input_source_t * input)
{
	if(NULL == input) return;
	input_source_private_cleanup(input->priv);
	free(input);
}

static int	input_source_set_uri(struct input_source * input, const char * uri)
{
	int rc = 0;
	int subtype = 0;
	char * cooked_uri = NULL;
	if(NULL == uri || !uri[0]) return -1;
	
	enum input_source_type type = input_source_type_parse_uri(uri, &cooked_uri, &subtype);
	if(type == input_source_type_unknown)
	{
		free(cooked_uri);
		return -1;
	}
	assert(cooked_uri);
	
	input_source_private_t * priv = input->priv;
	assert(priv && priv->input == input && input->priv == priv);

	input->type = type;
	input->subtype = subtype;

	priv->uri = strdup(cooked_uri);
	
	switch(type)
	{
	case input_source_type_rtsp:
	case input_source_type_http:
	case input_source_type_https:
	case input_source_type_v4l2:
		rc = video_source_set_uri(priv->video_src, input, cooked_uri, subtype);
		break;
	case input_source_type_file:
		if(subtype == input_source_subtype_video)
		{
			rc = video_source_set_uri(priv->video_src, input, cooked_uri, subtype);
		}else
		{
			rc = static_file_source_set_uri(priv->file_src, input, cooked_uri, subtype);
		}
		break;
	case input_source_type_fsnotify:
		rc = fsnotify_source_set_uri(priv->fsnotify_src, input, cooked_uri, subtype);
		break;
	default:
		fprintf(stderr, "[ERROR]::%s(%s):: unknown input type(%d)\n",
			__FUNCTION__, uri,
			(int)type);
		exit(1);
	}
	
	free(cooked_uri);
	return rc;
}

static int 	input_source_restart(struct input_source * input)
{
	return 0;
}

static int 	input_source_on_new_frame(struct input_source * input, const input_frame_t * frame)
{
	input->set_frame(input, frame);	// default process
	return 0;
}

static long input_source_set_frame(struct input_source * input, const input_frame_t * new_frame)
{
	input_source_private_t * priv = input->priv;
	input_frame_t * frame = priv->frame_buffer[1];
	if(NULL == frame)
	{
		frame = calloc(1, sizeof(*frame));
	}
	assert(frame);
	input_frame_copy(frame, new_frame);
	
	pthread_mutex_lock(&priv->mutex);
	priv->frame_buffer[1] = priv->frame_buffer[0];
	priv->frame_buffer[0] = frame;
	//~ debug_printf("%s()::frame_size=%d x %d, channels=%d\n", 
		//~ __FUNCTION__,
		//~ frame->image->width, frame->image->height, frame->image->channels);
	pthread_mutex_unlock(&priv->mutex);

	return 0;
}

static long input_source_get_frame(struct input_source * input, long prev_frame, input_frame_t * dst)
{
	input_source_private_t * priv = input->priv;
	pthread_mutex_lock(&priv->mutex);
	input_frame_t * frame = priv->frame_buffer[0];
	if(NULL == frame) {
		pthread_mutex_unlock(&priv->mutex);
		return -1;
	}

	if(prev_frame >= 0 && prev_frame < priv->frame_number)		// only copy new frame to dst
	{ 
		input_frame_copy(dst, frame);
	}
	pthread_mutex_unlock(&priv->mutex);
	return priv->frame_number;
}

/***********************************************************************************
 * video_source
***********************************************************************************/
static gboolean video_source_on_eos(GstBus * bus, GstMessage * message, video_source_t * src)
{
	debug_printf("%s()...\n", __FUNCTION__);
	return FALSE;
}
static gboolean video_source_on_error(GstBus * bus, GstMessage * message, video_source_t * src)
{
	debug_printf("%s()...\n", __FUNCTION__);
	src->input->stop(src->input);
	return FALSE;
}

static void video_source_on_bgra_filter(GstElement * filter, GstBuffer * buffer, video_source_t * src)
{
//	debug_printf("%s()...\n", __FUNCTION__);
	input_source_t * input = src->input;
	input_source_private_t * priv = input->priv;
	assert(input && priv);

	int width = 0;
	int height = 0;

	GstPad * pad = gst_element_get_static_pad(filter, "src");
	assert(pad);

	GstCaps * caps = gst_pad_get_current_caps(pad);
	assert(caps);

	GstStructure * info = gst_caps_get_structure(caps, 0);
	assert(info);

	gboolean rc = FALSE;
	rc = gst_structure_get_int(info, "width", &width); assert(rc);
	rc = gst_structure_get_int(info, "height", &height); assert(rc);

	if(width != priv->height && height != priv->height)
	{
		priv->frame_number = 0;	// reset
		priv->width = width;
		priv->height = height;

		const char * fmt = gst_structure_get_string(info, "format");
		printf("[thread_id: %ld]::uri: %s\n",
                 (long)pthread_self(), priv->uri);
        printf("== format: %s, size=%dx%d\n", fmt, width, height);
	}
	gst_object_unref(pad);

	GstMapInfo map[1];
	memset(map, 0, sizeof(map));

	input_frame_t frame[1] = {{
		.type = input_frame_type_bgra,
	}};
	rc = gst_buffer_map(buffer, map, GST_MAP_READ);
	if(rc)
	{
		bgra_image_init(frame->image, width, height, map->data);
		gst_buffer_unmap(buffer, map);
	}

	priv->frame_number++;
	if(input->on_new_frame)
	{
		input->on_new_frame(input, frame);
	}else
	{
		input->set_frame(input, frame);
		
	}
	input_frame_clear(frame);
	return;
}


static int video_source_set_uri(video_source_t * src, input_source_t * input, const char * cooked_uri, int subtype)
{
	//~ input_source_private_t * priv = input->priv;
	
	input->play = video_source_play;
	input->stop = video_source_stop;
	input->pause = video_source_pause;

	enum input_source_type type = input->type;

#define RTSP_SRC_FMT		"rtspsrc location=\"%s\" ! decodebin ! videoconvert "
#define HTTP_SRC_FMT		"souphttpsrc location=\"%s\" ! decodebin ! videoconvert "
#define HLS_SRC_FMT			"souphttpsrc location=\"%s\" ! hlsdemux ! decodebin ! videoconvert "
#define V4L2_SRC_FMT		"v4l2src device=%s "
#define FILE_SRC_FMT		"filesrc location=\"%s\" ! decodebin "

#define BGRA_PIPELINE 	" ! videoconvert "						\
						" ! videoscale ! video/x-raw,format=BGRA,width=640,height=480 ! videoconvert ! identity name=\"filter\" "		\
						" ! fakesink sync=true"														

					//	" ! ximagesink "
					//	" ! fakesink sync=true"														
	

	char gst_command[8192] = "";
	int cb = 0;
	switch(type)
	{
	case input_source_type_rtsp:
		cb = snprintf(gst_command, sizeof(gst_command),
			RTSP_SRC_FMT BGRA_PIPELINE,
			cooked_uri);
		break;
	case input_source_type_http:
	case input_source_type_https:
		if(subtype == input_source_subtype_hls)
		{
			cb = snprintf(gst_command, sizeof(gst_command),
				HLS_SRC_FMT BGRA_PIPELINE,
				cooked_uri);
		}else
		{
			cb = snprintf(gst_command, sizeof(gst_command),
				HTTP_SRC_FMT BGRA_PIPELINE,
				cooked_uri);
		}
		break;
	case input_source_type_file:
		assert(subtype == input_source_subtype_default || subtype == input_source_subtype_video);
		cb = snprintf(gst_command, sizeof(gst_command), FILE_SRC_FMT BGRA_PIPELINE,
			cooked_uri);
		break;
	case input_source_type_v4l2:
		cb = snprintf(gst_command, sizeof(gst_command), V4L2_SRC_FMT BGRA_PIPELINE,
			cooked_uri);
		break;
	default:
		fprintf(stderr, "[ERROR]::unknown input type %d\n", (int)type);
		exit(1);
	}

	assert(cb > 0);
	debug_printf("%s(): \ngst_command: %s\n",
			__FUNCTION__,
			gst_command);

	
	GError * gerr = NULL;
	GstElement * pipeline = gst_parse_launch(gst_command, &gerr);
	if(NULL == pipeline || gerr)
	{
		if(gerr) {
			fprintf(stderr, "[ERROR]::gst_pase_launch failed: %s\n", gerr->message);
			g_error_free(gerr);
		}
		return -1;
	}

	GstElement * filter = gst_bin_get_by_name(GST_BIN(pipeline), "filter");
	assert(filter);

	g_signal_connect(filter, "handoff", G_CALLBACK(video_source_on_bgra_filter), src);

	src->pipeline = pipeline;
	src->filter = filter;

	GstBus * bus = gst_element_get_bus(pipeline);
	assert(bus);

	gst_bus_add_signal_watch(bus);
	g_signal_connect(bus, "message::eos", G_CALLBACK(video_source_on_eos), src);
	g_signal_connect(bus, "message::error", G_CALLBACK(video_source_on_error), src);

	GMainLoop * loop = g_main_loop_new(NULL, FALSE);
	assert(loop);

	src->loop = loop;
	
	src->gst_command = strdup(gst_command);
	return 0;
}
static int video_source_play(input_source_t * input)
{
	input_source_private_t * priv = input->priv;
	video_source_t * src = (video_source_t *)priv->video_src;
	assert(src->input == input && priv->input == input);
	
	GstStateChangeReturn ret_code = gst_element_set_state(src->pipeline, GST_STATE_PLAYING);
	if(ret_code == GST_STATE_CHANGE_FAILURE) return -1;

	priv->is_running = 1;
	return 0;
}
static int video_source_stop(input_source_t * input)
{
	input_source_private_t * priv = input->priv;
	video_source_t * src = (video_source_t *)priv->video_src;
	assert(src->input == input && priv->input == input);
	
	GstStateChangeReturn ret_code = gst_element_set_state(src->pipeline, GST_STATE_NULL);
	if(ret_code == GST_STATE_CHANGE_FAILURE) return -1;

	GstState state = 0;
	ret_code = gst_element_get_state(src->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);

	gst_object_unref(src->pipeline);
	src->pipeline = NULL;

	priv->is_running = 0;
	
	return 0;
}
static int video_source_pause(input_source_t * input)
{
	input_source_private_t * priv = input->priv;
	video_source_t * src = (video_source_t *)priv->video_src;
	assert(src->input == input && priv->input == input);
	
	GstStateChangeReturn ret_code = gst_element_set_state(src->pipeline, GST_STATE_PAUSED);
	
	debug_printf("pause() = %d\n", ret_code);
	if(ret_code == GST_STATE_CHANGE_FAILURE) return -1;

	
	priv->is_running = 0;
	priv->is_paused = 1;
	return 0;
}

static void video_source_cleanup(video_source_t * src)
{
	return;
}



/***********************************************************************************
 * static_file_source
***********************************************************************************/
//~ typedef struct static_file_source
//~ {
	//~ /* base object */
	//~ input_source_t * input;		// base object
	//~ input_frame_t * frame[2];

	//~ /* private context */
	//~ char path_name[PATH_MAX];
	
	//~ /* virtual methods */
	//~ int (* play)(input_source_t * input);
	//~ int (* stop)(input_source_t * input);
	//~ int (* pause)(input_source_t * input);
//~ }static_file_source_t;
static int static_file_source_set_uri(static_file_source_t * src, input_source_t * input, const char * cooked_uri, int subtype)
{
	debug_printf("%s()::cooked_uri=%s", __FUNCTION__, cooked_uri);
	
	src->input = input;
	input->play 	= static_file_source_play;
	input->stop 	= static_file_source_stop;
	input->pause 	= static_file_source_pause;
	
	input_frame_t * frame = src->frame;
	strncpy(src->path_name, cooked_uri, sizeof(src->path_name));
	
	int rc = bgra_image_load_from_file(frame->image, cooked_uri);
	if(rc)
	{
		fprintf(stderr, "[WARNING]::%s()::open image file failed: %s\n", __FUNCTION__, cooked_uri);
		return -1;
	}
	
	if(input->on_new_frame)
	{
		input->on_new_frame(input, frame);
	}else
	{
		input->set_frame(input, frame);
	}
	input_source_private_t * priv = input->priv;
	++priv->frame_number;

	return 0;
}

static int static_file_source_play(input_source_t * input)
{
	return 0;
}

static int static_file_source_stop(input_source_t * input)
{
	return 0;
}

static int static_file_source_pause(input_source_t * input)
{
	return 0;
}

static void static_file_source_cleanup(static_file_source_t * src)
{
	if(NULL == src) return;
	input_frame_clear(src->frame);
	return;
}

/***********************************************************************************
 * fsnotify_source
***********************************************************************************/

#include <unistd.h>
#include <libgen.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

//~ static const int s_default_watch_flags = IN_ACCESS
	//~ | IN_ATTRIB
	//~ | IN_CLOSE_WRITE
	//~ | IN_CLOSE_NOWRITE
	//~ | IN_CREATE
	//~ | IN_DELETE
	//~ | IN_DELETE_SELF
	//~ | IN_MODIFY
	//~ | IN_MOVE_SELF
	//~ | IN_MOVED_FROM
	//~ | IN_MOVED_TO
	//~ | IN_OPEN
//~ //	| IN_DONT_FOLLOW
	//~ | 0;


static int input_source_on_fs_notify(filesystem_notify_t * fsnotify, const fsnotify_data_t * notify_data)
{
	fsnotify_source_t * src = fsnotify->user_data;
	input_source_t * input = src->input;
	
	fsnotify_data_t * child  = fsnotify->get_data(fsnotify, 0);
	fsnotify_data_t * parent = fsnotify->get_data(fsnotify, 1);
	
	printf("%s()::notify_data.path = %s, mode=%d\n", __FUNCTION__, notify_data->path_name, src->mode);
	
	if(notify_data == child)			// filesystem events under current directroy
	{
		debug_printf("[INFO]::child events");
		if(notify_data->event->mask & IN_CLOSE_WRITE)
		{
			printf("\t --> [new] file: %s\n", notify_data->event->name);
			
			char full_name[PATH_MAX] = "";
			int cb = snprintf(full_name, sizeof(full_name), "%s/%s", notify_data->path_name, notify_data->event->name);
			assert(cb <= PATH_MAX);
			if(src->mode == 0) // json and images
			{
				char * images_path = src->images_path;
				assert(images_path && images_path[0]);
				
				char image_file[PATH_MAX] = "";
				snprintf(image_file, sizeof(image_file), "%s/%s", images_path, notify_data->event->name);
				char * p_ext = strrchr(image_file, '.');
				if(NULL == p_ext) return -1;
				
				strcpy(p_ext, ".jpg");
				if(0 != check_file(image_file))  strcpy(p_ext, ".png");
				if(0 != check_file(image_file)) return -1;
				
				bgra_image_load_from_file(src->frame->image, image_file);
				
				json_object * jresult = src->frame->meta_data;
				src->frame->meta_data = NULL;
				
				if(jresult)
				{
					json_object_put(jresult);
					jresult = NULL;
				}
				
				jresult = json_object_from_file(full_name);
				src->frame->meta_data = jresult;
				
			}else
			{
				bgra_image_load_from_file(src->frame->image, full_name);
			}
			
			if(input->on_new_frame)
			{
				input->on_new_frame(input, src->frame);
			}else
			{
				input->set_frame(input, src->frame);
			}
			input_source_private_t * priv = input->priv;
			++priv->frame_number;
			
		}else if(notify_data->event->mask & IN_DELETE_SELF)
		{
			printf("\t --> [delete self]: %s\n", notify_data->path_name);
		}else if(notify_data->event->mask & IN_MOVE_SELF)
		{
			printf("\t --> [move self]: %s\n", notify_data->path_name);
		}else
		{
			fprintf(stderr, "[ERROR]::%s(%d)::%s()::unexpected event 0x%.8x\n", 
				__FILE__, __LINE__, __FUNCTION__,
				(unsigned int)notify_data->event->mask);
			exit(1);
		}
	}else if(notify_data == parent)	// check if current directory was DELETED/MOVED/RECREATED ??
	{
		debug_printf("[INFO]::parent events");
		if(notify_data->event->mask & IN_CREATE)
		{
			printf("\t --> [IN_CREATE] file: %s\n", notify_data->event->name);
		}else if(notify_data->event->mask & IN_MOVED_FROM)
		{
			char path_name[PATH_MAX] = "";
			int cb = snprintf(path_name, sizeof(path_name), "%s/%s", parent->path_name, notify_data->event->name);
			assert(cb <= PATH_MAX);
			
			printf("\t --> [IN_MOVED_FROM] file: %s, child_path=%s\n", path_name, child->path_name);
			
			if(strcmp(path_name, child->path_name) == 0)
			{
				fs_move_event_queue_enter(src->queue, notify_data->event->cookie, child);
			}
		}else if(notify_data->event->mask & IN_MOVED_TO)
		{
			printf("\t --> [IN_MOVED_TO] file: %s\n", notify_data->event->name);
			
			fs_move_event_data_t * moved_path = fs_move_event_queue_leave(src->queue, notify_data->event->cookie);
			if(moved_path)
			{
				char path_name[PATH_MAX] = "";
				int cb = snprintf(path_name, sizeof(path_name), "%s/%s", parent->path_name, notify_data->event->name);
				assert(cb <= PATH_MAX);
				
				assert(moved_path->data == child);
				printf("watching folder was renamed: from=%s, to=%s\n", moved_path->data->path_name, path_name);
				
				fsnotify->update_watch(fsnotify, 0, path_name, child->flags);
				
				free(moved_path);
			}
			
			
		}else if(notify_data->event->mask & IN_DELETE)
		{
			printf("\t --> [IN_DELETE] file: %s\n", notify_data->event->name);
		}
		else
		{
			fprintf(stderr, "[ERROR]::%s(%d)::%s()::unexpected event 0x%.8x\n", 
				__FILE__, __LINE__, __FUNCTION__,
				(unsigned int)notify_data->event->mask);
			exit(1);
		}
	}
	
	return 0;
}

#define AUTO_FREE_PTR __attribute__((cleanup(auto_free_ptr)))
static void auto_free_ptr(void * ptr)
{
	void * p = *(void **)ptr;
	if(p)
	{
		free(p);
		*(void **)ptr = NULL;
	}
	return;
}

static int fsnotify_source_set_uri(fsnotify_source_t * src, input_source_t * input, const char * cooked_uri, int subtype)
{
	assert(cooked_uri);
	
	input->play 	= fsnotify_source_play;
	input->stop 	= fsnotify_source_stop;
	input->pause 	= fsnotify_source_pause;
	
	src->input = input;
	filesystem_notify_t * fsnotify = filesystem_notify_init(&src->fsnotify[0], src);
	assert(fsnotify && fsnotify == &src->fsnotify[0]);
	fsnotify->on_notify = input_source_on_fs_notify;	// set custom callback
	
	/* parse fsnotify uri */
	AUTO_FREE_PTR char * uri = strdup(cooked_uri);
	
	char * token = NULL;
	char * key, *value = NULL;
	char images_path[PATH_MAX] = "input/images";
	char json_path[PATH_MAX] = "input/json";
	int auto_delete_file = 1;
	int mode = 0;
	
	char * query_string = strchr(uri, '?');
	if(query_string)
	{
		*query_string++ = '\0';
		
		key = strtok_r(query_string, "&", &token);
		while(key)
		{
			value = strchr(key, '=');
			if(value) *value++ = '\0';
			key = trim(key, NULL);
			if(value) 
			{
				value = trim(value, NULL);
				if(strcasecmp(key, "images") == 0) strncpy(images_path, value, sizeof(images_path));
				else if(strcasecmp(key, "json") == 0) strncpy(json_path, value, sizeof(json_path));
				else if(strcasecmp(key, "mode") == 0) mode = atoi(value);
				else if(strcasecmp(key, "auto_delete_file") == 0) auto_delete_file = atoi(value);
			}
			
			key = strtok_r(NULL, "&", &token);
		}
	}
	strncpy(src->images_path, images_path, sizeof(src->images_path));
	strncpy(src->json_path, json_path, sizeof(src->json_path));
	src->mode = mode;
	src->auto_delete_file = auto_delete_file;
	
	/* add directory watch */
	struct stat st[1];
	memset(st, 0, sizeof(st));
	char * path = (mode == 0)?json_path:images_path; 
	
	int rc = stat(path, st);
	if(rc != 0) {
		perror("fsnotify_source_set_uri()::stat");
		return -1;
	}
	assert(0 == rc);
	if(!S_ISDIR(st->st_mode))
	{
		fprintf(stderr, "[ERROR]: invalid path_name: %s. (not directory)\n", path);
		exit(1);
	}
	int watch_flags = IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF;
	fsnotify->add_watch(fsnotify, path, watch_flags);
	

	// add parent folder watch ( watching [delete / move_from / move_to] events)
	char * parent_folder = dirname(path);
	assert(parent_folder);
	rc = stat(parent_folder, st);
	assert(0 == rc);
	if(!S_ISDIR(st->st_mode))
	{
		fprintf(stderr, "[ERROR]: invalid parent path_name: %s. (not directory)\n", parent_folder);
		exit(1);
	}
	
	watch_flags = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
	fsnotify->add_watch(fsnotify, parent_folder, watch_flags);
	
	return 0;
}

static int fsnotify_source_play(input_source_t * input)
{
	input_source_private_t * priv = input->priv;
	fsnotify_source_t * src = priv->fsnotify_src;
	
	src->fsnotify->run(src->fsnotify, 1);
	return 0;
}

static int fsnotify_source_stop(input_source_t * input)
{
	return 0;
}

static int fsnotify_source_pause(input_source_t * input)
{
	return 0;
}

static void fsnotify_source_cleanup(fsnotify_source_t * src)
{
	return;
}
