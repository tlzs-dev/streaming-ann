#ifndef _ANN_PLUGIN_H_
#define _ANN_PLUGIN_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <json-c/json.h>

#define IO_PLUGINS_PREFIX "libioplugin"
#define AI_PLUGINS_PREFIX "libaiplugin"

/* ai-plugin DLLexport function types */
typedef const char * (*ann_plugin_get_type_function)(void);
typedef int (* ann_plugin_init_function)(void * object /* io-input or ai-engine */, json_object * jconfig);

typedef struct ann_plugin
{
	void * handle;
	void * user_data;
	
	char * filename;				
	char * type;					// plugin_type_string
	void * query_interface;			// TODO:: query custom functions

	// virtual interfaces
	void * init_func;	// init_function pointer
	
}ann_plugin_t;
ann_plugin_t * ann_plugin_new(const char * filename, void * user_data);
void ann_plugin_free(ann_plugin_t * plugin);

typedef struct ann_plugins_helpler
{
	void * user_data;
	ssize_t max_size;
	ssize_t num_plugins;
	ann_plugin_t ** plugins;

	// public functions
	ssize_t (* load)(struct ann_plugins_helpler * helpler, const char * plugins_path);
	int (* add)(struct ann_plugins_helpler * helpler, const char * filename);
	int (* remove)(struct ann_plugins_helpler * helpler, const char * filename);
	ann_plugin_t * (* find)(struct ann_plugins_helpler * helpler, const char * sz_type);
}ann_plugins_helpler_t;
ann_plugins_helpler_t * ann_plugins_helpler_init(ann_plugins_helpler_t * helpler, const char * plugins_path, void * user_data);
ann_plugins_helpler_t * ann_plugins_helpler_get_default(void);
void ann_plugins_helpler_cleanup(ann_plugins_helpler_t * helpler);


#ifdef __cplusplus
}
#endif
#endif
