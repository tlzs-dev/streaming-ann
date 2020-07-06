# Streaming ANN:

## Table of Contents
	
	I. Plugins Programming Guide:
	II. Streaming-ann SDK API Reference
	III. Use Cases
	IV. Built-in Plugins and Modules

## I. Plugins Programming Guide:
1. plugins definitions:

	**Each plugin must contain and implement the following 3 definitions:**
	
	(1) a typename string: (plugin_type)

	```	#define ANN_PLUGIN_TYPE_STRING "io-plugin::httpclient" ```

	(2)  ann_plugin_get_type() function:

	```	const char * ann_plugin_get_type(void) { return ANN_PLUGIN_TYPE_STRING; } ```

	(3) ann_plugin_init() function:

		int ann_plugin_init (
			void * object,			// io_input_t or ai_engine_t
			json_object * jconfig	// object-settings
		);
	
2. plugin class and virtual interfaces:

	(1) class: 

		typedef struct ann_plugin
		{

			void * handle;					// <== dlopen()
			void * user_data;				// not used
			
			char * filename;				// plugin filename
			char * type;					// plugin_type string
			void * query_interface;			// TODO:: query custom functions
			
			// virtual interfaces 
			void * init_func; 			// init_function pointer
			
		}ann_plugin_t;

	(2) virtual interfaces:
		
		typedef const char * (*ann_plugin_get_type_function)(void);
		typedef int  (* ann_plugin_init_function)(void * object, json_object * jconfig);


## II. Streaming-ann SDK API Reference

** Description: **

Since this SDK uses OOP implemented in C,
Most of the class instances needs to be initialized by calling '_classname_ _init()' API.

Use ‘_classname_ _cleanup ()’ to release resources

1. ** plugins manager (helpler class): **

	** (1) Definition: **

		typedef struct ann_plugins_helpler
		{
		
			void * user_data;		// not used
			ssize_t max_size;		// max plugins
			ssize_t num_plugins;	// plugins count
			ann_plugin_t ** plugins;	
		
			// public functions
			ssize_t (* load)(struct ann_plugins_helpler * helpler, const char * plugins_path);
			int (* add)(struct ann_plugins_helpler * helpler, const char * filename);
			int (* remove)(struct ann_plugins_helpler * helpler, const char * filename);
			plugin_t * (* find)(struct ann_plugins_helpler * helpler, const char * sz_type);
		}ann_plugins_helpler_t;

	** (2) API **
		ann_plugins_helpler_t * ann_plugins_helpler_init(

			ann_plugins_helpler_t * helpler,	// must be NULL
			const char * plugins_path,			// plugin files path
			void * user_data					// not used
		);			
		ai_plugins_helpler_t * ai_plugins_helpler_get_default(void);
		void ai_plugins_helpler_cleanup(ai_plugins_helpler_t * helpler);

	** (3) Example: ** (source-code: tests/test-plugins.c )
	
		The following program demonstrates the usuage of 'ann_plugins_helpler' class.
		
		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>
		#include <assert.h>
		#include "ann-plugin.h"

		void ann_plugins_dump(ann_plugins_helpler_t * helpler);
		int main(int argc, char **argv)
		{
			const char * plugins_path = "plugins";		// plugin files path
			if(argc > 1) plugins_path = argv[1];

			/* init plugins manager, MUST be initialized before any another streaming-ann-api calling */
			ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, plugins_path, NULL);

			/* check plugins info */
			ann_plugins_dump(helpler);

			/* cleanup */
			ann_plugins_helpler_cleanup(helpler);
			return 0;
		}

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


2. ** io-module plugins **

	Description:
		streaming-ann API Input/Output Module;
			
	(1) **input_frame** class: (ANN [input / output] data buffer)
	
		enum input_frame_type
		{
			input_frame_type_invalid = -1,
			input_frame_type_unknown = 0,
			input_frame_type_bgra = 1,
			input_frame_type_jpeg = 2,
			input_frame_type_png  = 3,

			input_frame_type_image_masks = 0x7FFF,
			input_frame_type_json_flag = 0x8000,
		};
	
		typedef struct input_frame
		{
			union
			{
				bgra_image_t bgra[1];
				bgra_image_t image[1];
				struct
				{
					unsigned char * data;
					int width;
					int height;
					int channels;
					int stride;
				};
			};
			ssize_t length;
			
			long frame_number;
			struct timespec timestamp[1];
			enum input_frame_type type;
			
			ssize_t cb_json;
			char * json_str;
			void * meta_data;	// json_object
		}input_frame_t;
		
		void input_frame_free(input_frame_t * frame);
		input_frame_t * input_frame_new();
		void input_frame_clear(input_frame_t * frame);

		int input_frame_set_bgra(input_frame_t * input, const bgra_image_t * bgra, const char * json_str, ssize_t cb_json);
		int input_frame_set_jpeg(input_frame_t * input, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json);
		int input_frame_set_png(input_frame_t * input, const unsigned char * data, ssize_t length, const char * json_str, ssize_t cb_json);

	(2) ** io_input ** class

		enum io_input_type
		{
			io_input_type_invalid = -1,
			io_input_type_default = 0,		// struct input-source
			io_input_type_memory = 1,		
			io_input_type_tcp_server,		// passive mode: listening user's inputs: tcp send
			io_input_type_http_server,		// passive mode: listening user's inputs: HTTP POST
			io_input_type_tcp_client,		// pull-mode, GET data from ip:port
			io_input_type_http_client,		// pull-mode, GET data from url
			io_input_type_custom = 999,		// custom plugins
			io_input_types_count
		};

		enum io_input_type io_input_type_from_string(const char * sz_type);

		typedef struct io_input
		{
			void * user_data;			// user's context
			void * priv;				// io-plugin's private data
			void * plugins_helpler;		// io_plugins_helpler_t
			
			enum io_input_type type;	// build-in
			char * input_type;			//
			char * name;				// input-id
			
			/* virtual interfaces: ( overided by io-plugin implementations ) */
			
			// Init and load settings, Each plugin has its own configuration format,
			// please refer to the sample json files in the plugins 'conf' folder
			int (* init)(struct io_input * input, json_object * jconfig);
			
			int (* run)(struct io_input * input);
			int (* stop)(struct io_input * input);
			void (* cleanup)(struct io_input * input);
			int (* load_config)(struct io_input * input, json_object * jconfig);	// reload settings
			int (* get_property)(struct io_input * input, const char * name, char ** p_value, size_t * p_length);
			int (* set_property)(struct io_input * input, const char * name, const char * value, size_t cb_value);
			
			// io_input::member_functions
			long (* get_frame)(struct io_input * input, long prev_frame, input_frame_t * frame);
			long (* set_frame)(struct io_input * input, const input_frame_t * frame);
			
			// user-defined callbacks
			int (* on_new_frame)(struct io_input * input, const input_frame_t * frame);

			// private data ( KEEP UNTOUCHED )
			void * frame_buffer;
			long frame_number;
		}io_input_t;

		io_input_t * io_input_init(io_input_t * input, const char * sz_type, void * user_data);
		void io_input_cleanup(io_input_t * input);

	(3) ** Example: ** ** (source-code: tests/test-io-inputs.c )
	
		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>
		#include <assert.h>
		#include <gst/gst.h>

		#include "ann-plugin.h"
		#include "io-input.h"

		#define IO_PLUGIN_DEFAULT "io-plugin::input-source"
		#define IO_PLUGIN_HTTPD	"io-plugin::httpd"
		#define IO_PLUGIN_HTTP_CLIENT	"io-plugin::httpclient"

		/* notification::callback when a new frame is available  */
		int test_on_new_frame(io_input_t * input, const input_frame_t * frame)
		{
			static long frame_number = 0;
			assert(frame && frame->data);
			char sz_type[200] = "";
			ssize_t cb_type = input_frame_type_to_string(frame->type, sz_type, sizeof(sz_type));
			assert(cb_type > 0);

			printf("==== frame_number: %ld ====\n", frame_number++);
			printf("\t: type: %s (0x%.8x)\n", sz_type, frame->type);
			printf("\t: image_size: %d x %d\n", frame->width, frame->height);
			return 0;
		}

		json_object * load_config(const char * conf_file, const char * plugin_type)
		{
			json_object * jconfig = NULL;
			if(conf_file) return json_object_from_file(conf_file);

			jconfig = json_object_new_object();
			assert(jconfig);

			// use default settings
			if(strcasecmp(plugin_type, IO_PLUGIN_DEFAULT) == 0)
			{
				// camera uri:  [rtsp/http/fsnotify/file]://camera_or_video_or_image_path,
				static const char * camera_id = "input1";
				static const char * camera_uri = "0";		// "0": local_camera (/dev/video0)
				
				json_object_object_add(jconfig, "name", json_object_new_string(camera_id));
				json_object_object_add(jconfig, "uri", json_object_new_string(camera_uri));
			}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTPD) == 0)
			{
				json_object_object_add(jconfig, "port", json_object_new_string("9001"));
			}else if(strcasecmp(plugin_type, IO_PLUGIN_HTTP_CLIENT) == 0)
			{
				json_object_object_add(jconfig, "url", json_object_new_string("http://localhost:9001"));
			}
			return jconfig;
		}

		static const char * plugin_type = IO_PLUGIN_DEFAULT;
		static const char * plugins_path = "plugins";
		int parse_args(int argc, char ** argv);

		volatile int quit;
		int main(int argc, char **argv)
		{
			int rc = 0;
			gst_init(&argc, &argv);
			parse_args(argc, argv);
			
			ann_plugins_helpler_init(NULL, plugins_path, NULL);
			io_input_t * input = io_input_init(NULL, plugin_type, NULL);
			assert(input);

			
			json_object * jconfig = load_config(NULL, plugin_type);
			rc = input->init(input, jconfig);
			assert(0 == rc);

			/*
			 * input->on_new_frame:  on new_frame available notification callback
			 * 	if this callback is set to NULL,
			 * 	use input->get_frame() to retrieve the lastest frame
			*/ 
			input->on_new_frame = test_on_new_frame; // nullable
			input->run(input);

			if(strcasecmp(plugin_type, IO_PLUGIN_HTTPD) == 0)	// httpd only, patch to libsoup 
			{
				GMainLoop * loop = g_main_loop_new(NULL, TRUE);
				g_main_loop_run(loop);
			}else
			{
				char command[200] = "";
				char * line = NULL;
				while((line = fgets(command, sizeof(command) - 1, stdin)))
				{
					if(line[0] == 'q') break;
					break;
				}
			}

			io_input_cleanup(input);
			return 0;
		}

		#include <getopt.h>
		int parse_args(int argc, char ** argv)
		{
			static struct option options[] = {
				{"plugins-dir", required_argument, 0, 'd'},
				{"plugins-type", required_argument, 0, 't'},
				{NULL}
			};
			int option_index = 0;
			while(1)
			{
				int c = getopt_long(argc, argv, "d:t:", options, &option_index);
				if(c == -1) break;

				switch(c)
				{
				case 'd': plugins_path = optarg; break;
				case 't': plugin_type = optarg; break;
				default:
					fprintf(stderr, "invalid args: %c\n", c);
					exit(1);
				}
			}
			return 0;
		}


3. ** ai-engine ** plugins:
	 
	Description: darknet /opencv-dnn / caffe / tensorflow / torch plugins

	(1) ** ai_tensor ** class
	
		enum ai_tensor_data_type
		{
			ai_tensor_data_type_unknown = -1,
			ai_tensor_data_type_float32 = 0,
			ai_tensor_data_type_float64,
			ai_tensor_data_type_uint8,
			ai_tensor_data_type_uint16,
			ai_tensor_data_type_uint32,
			ai_tensor_data_type_uint64,
			ai_tensor_data_types_count,

			ai_tensor_data_type_masks = 0x7fff,
			ai_tensor_data_type_gpu_flags = 0x8000,
		};
		typedef struct ai_tensor
		{
			enum ai_tensor_data_type type;
			void * 		data;
			int_dim4 dim[1];
			size_t length;
		}ai_tensor_t;
			
		ai_tensor_t * ai_tensor_init(ai_tensor_t * tensor, enum ai_tensor_data_type type, const int_dim4 * size, const void * data);
		void ai_tensor_clear(ai_tensor_t * tensor);
		int ai_tensor_resize(ai_tensor_t * tensor, const int_dim4 * new_size);

	(2) ** ai-engine ** class
	
		typedef struct ai_engine
		{
			void * user_data;
			void * priv;			// ai_plugin_t *
			
			// virtual functions ( overided by plugins )
			int (* init)(struct ai_engine * engine, json_object * jconfig);
			void (* cleanup)(struct ai_engine * engine);
			
			int (* load_config)(struct ai_engine * engine, json_object * jconfig);
			int (* predict)(struct ai_engine * engine, const input_frame_t * frame, json_object ** p_jresults);
			int (* update)(struct ai_engine * engine, const ai_tensor_t * truth);
			int (* get_property)(struct ai_engine * engine, const char * name, void ** p_value);
			int (* set_property)(struct ai_engine * engine, const char * name, const void * value, size_t length);

			// public member functions
			ai_tensor_t * (* get_workspace)(struct ai_engine * engine);		// pre-allocated global memory (GPU or CPU)
		}ai_engine_t;

		ai_engine_t * ai_engine_init(ai_engine_t * engine, const char * plugin_type, void * user_data);
		void ai_engine_cleanup(ai_engine_t * engine);

	(3) Examples: ( source-code: tests/test-ai-engine.c)
	
		#include <stdio.h>
		#include <stdlib.h>
		#include <string.h>
		#include <assert.h>

		#include <json-c/json.h>
		#include "ann-plugin.h"
		#include "ai-engine.h"

		#include "utils.h"
		#include "input-frame.h"

		json_object * load_config(const char * conf_file, const char * engine_type)
		{
			json_object * jconfig = NULL;
			if(conf_file) jconfig = json_object_from_file(conf_file);

			if(NULL == jconfig)
			{
				jconfig = json_object_new_object();
				json_object_object_add(jconfig, "conf_file", json_object_new_string("models/yolov3.cfg"));
				json_object_object_add(jconfig, "weights_file", json_object_new_string("models/yolov3.weights"));
			}

			return jconfig;
		}

		static const char * aiengine_type = "ai-engine::darknet";
		int main(int argc, char **argv)
		{
			int rc = -1;
			assert(ann_plugins_helpler_init(NULL, "plugins", NULL));

			ai_engine_t * engine = ai_engine_init(NULL, aiengine_type, NULL);
			assert(engine);

			json_object * jconfig = load_config(NULL, aiengine_type);
			rc = engine->init(engine, jconfig);
			assert(0 == rc);

			// test
			const char * image_file = "1.jpg";
			input_frame_t * frame = input_frame_new();
			unsigned char * jpeg_data = NULL;
			ssize_t cb_data = load_binary_data(image_file, &jpeg_data);
			assert(jpeg_data && cb_data > 0);
			
			rc = input_frame_set_jpeg(frame, jpeg_data, cb_data, NULL, 0);
			assert(0 == rc && frame->data && frame->width > 0 && frame->height > 0 && frame->length > 0);
			frame->length = frame->width * frame->height * 4;
			
			json_object * jresults = NULL;
			rc = engine->predict(engine, frame, &jresults);
			if(0 == rc)
			{
				fprintf(stderr, "detections: %s\n", json_object_to_json_string_ext(jresults, JSON_C_TO_STRING_PRETTY));
			}

			input_frame_free(frame);
			if(jresults) json_object_put(jresults);
			if(jconfig) json_object_put(jconfig);

			ai_engine_cleanup(engine);
			return 0;
		}

## III. Use Case:
1. local-input --> local-AIengine --> local-output

	+ Local PC:
        	
		[input]		: io_input_t * input = io_input_init(NULL, "ioplugin::default", ctx);

		[AI-engine]	: ai_engine_t * engine1 = ai_engine_init(NULL, "aiengine::darknet", ctx);
					  ai_engine_t * engine2 = ai_engine_init(NULL, "aiengine::cvdnn", ctx);
		
        [output]	: json_object * jresults;

	Example:
	
		int main() {
			ann_plugins_helpler_init(NULL, plugin_path, NULL);
			ai_context * ctx = new_ctx();
			
			
			ctx->input = input;
			ctx->engines_count = 2;
			ctx->engines[0] = engine1;
			ctx->engines[1] = engine2;
			
			input->init(input, jinput);
			input->on_new_frame = on_new_frame;
			input->run(input);
			
			engine1->init(engine1, jdarknet);
			engine2->init(engine2, jfacenet);
		
			g_main_loop_run();

			cleanup();
			return 0;
		}
		
		int on_new_frame(io_input_t * input, const input_frame_t * frame)
		{
			ai_context * ctx = input->user_data;
			for(int i = 0; i < ctx->engines_count; ++i)
			{
				json_object * jresults = NULL;
				ai_engine_t * engine = ctx->enginnes[i];
				ssize_t count = engine->predict(engine, frame, &jresults);
				if(count && jresults)
				{
					// output
				}
				if(jresults) json_object_put(jresults);
			}
			return 0;
		}


2. local-input --> remote-AIengine --> local-output

	+ Local PC:
	
		[input]		: io_input_t * input = io_input_init(NULL, "ioplugin::default", NULL);
		
		[AI-engine] : io_input_t * ai_request = io_input_init(NULL, "ioplugin::httpclient", NULL);
		
		[output]	: ai_request->get_frame()->json_str 

	+ Remote AI Server:
	
		[input]		: io_input_t * input = io_input_init(NULL, "ioplugin::httpd", ctx);
		
		[AI-engine]	: ai_engine_t * engine1 = ai_engine_init(NULL, "aiengine::darknet", ctx);
		
					ai_engine_t * engine2 = ai_engine_init(NULL, "aiengine::cvdnn", ctx);
					
					on_new_frame() --> engines->predict();
					
		[output]	: http response: (content-type: application/json)

3. remote-input --> local-AIengine 

	+ Remote:

		**input**	: io_input_t * input = io_input_init(NULL, "ioplugin::default", NULL);
		
		**output** : io_input_t * ai_request = io_input_init(NULL, "ioplugin::httpclient", NULL);
		
		
	+ Local PC:
	
		**input**	: io_input_t * input = io_input_init(NULL, "ioplugin::httpd", ctx);
		
		**AI-engine**: ai_engine_t * engine1 = ai_engine_init(NULL, "aiengine::darknet", ctx);
		
					  ai_engine_t * engine2 = ai_engine_init(NULL, "aiengine::cvdnn", ctx);
					  
					  engines->predict() --> Redenderer(Merge(frame+json)) --> mjpg->set_frame();
					  
		**output** : motion-jpeg server
					


## IV. Built-in Plugins and Modules
		1. io-input plugins:
			+ default:  
			+ http-server
			+ http-client

		2. ai-plugins:
			+ darknet	   (YoloV3, resnet101, resnet152, vgg-16, yolo9000, rnn, go)
			+ opencv-dnn (face detection / facerecognition)
			+ customize ai-engines

		3. Motion-JPEG Server
