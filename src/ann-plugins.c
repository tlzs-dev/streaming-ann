/*
 * ai-plugin.c
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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

#include <dlfcn.h>

#include "ann-plugin.h"

#define MAX_PLUGINS (256)
static int ann_plugins_helpler_resize(ann_plugins_helpler_t * helpler, ssize_t new_size)
{
	if(new_size <= 0) new_size = MAX_PLUGINS;
	if(new_size <= helpler->max_size) return 0;
	
	else new_size = (new_size + MAX_PLUGINS - 1) / MAX_PLUGINS * MAX_PLUGINS;
	ann_plugin_t ** plugins = realloc(helpler->plugins, sizeof(*plugins) * new_size);
	assert(plugins);
	memset(plugins + helpler->max_size, 0, (new_size - helpler->max_size) * sizeof(*plugins));
	helpler->plugins = plugins;
	helpler->max_size = new_size;
	return 0;
}
#undef MAX_PLUGINS

static int ann_plugins_helpler_add(ann_plugins_helpler_t * helpler, const char * filename)
{
	assert(filename);
	for(int i = 0; i < helpler->num_plugins; ++i)
	{
		ann_plugin_t * plugin = helpler->plugins[i];
		if(NULL == plugin) continue;
		
		if(strcmp(plugin->filename, filename) == 0) return 1;		// added
	}

	int rc = ann_plugins_helpler_resize(helpler, helpler->num_plugins + 1);
	assert(0 == rc);

	ann_plugin_t * plugin = ann_plugin_new(filename, helpler->user_data);
	if(plugin)
	{
		helpler->plugins[helpler->num_plugins++] = plugin;
		return 0;
	}
	return -1;
}

static int ann_plugins_helpler_remove(ann_plugins_helpler_t * helpler, const char * filename)
{
	for(int i = 0; i < helpler->num_plugins; ++i)
	{
		ann_plugin_t * plugin = helpler->plugins[i];
		if(NULL == plugin) continue;
		
		if(strcmp(plugin->filename, filename) == 0) {
			--helpler->num_plugins;
			ann_plugin_free(plugin);
			helpler->plugins[i] = helpler->plugins[helpler->num_plugins];
			helpler->plugins[helpler->num_plugins] = NULL;
			return 0;
		}
	}
	return -1;
}

static ann_plugin_t * ann_plugins_helpler_find(ann_plugins_helpler_t * helpler, const char * plugin_type)
{
	if(NULL == plugin_type) return NULL;
	for(int i = 0; i < helpler->num_plugins; ++i)
	{
		ann_plugin_t * plugin = helpler->plugins[i];
		if(NULL == plugin) continue;
		
		if(strcmp(plugin->type, plugin_type) == 0) return plugin;
	}
	return NULL;
}



int ann_plugins_file_filter(const struct dirent * entry)
{
	const char * prefix = strstr(entry->d_name, AI_PLUGINS_PREFIX);
	if(NULL == prefix) prefix = strstr(entry->d_name, IO_PLUGINS_PREFIX);
	
	const char * p_ext = NULL;
	if(prefix)
	{
		p_ext = strrchr(prefix, '.');
		return (p_ext && strcmp(p_ext, ".so") == 0 );
	}
	return 0;
}

static ssize_t ann_plugins_helpler_load(ann_plugins_helpler_t * helpler, const char * plugins_path)
{
	if(NULL == plugins_path) return -1;
	struct stat st[1];
	memset(st, 0, sizeof(st));

	int rc = stat(plugins_path, st);
	if(rc) {
		fprintf(stderr, "%s()::stat() failed: %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	if(S_ISREG(st->st_mode))		// single file
	{
		rc = helpler->add(helpler, plugins_path);
	}else if(S_ISDIR(st->st_mode))
	{
		struct dirent ** filelist = NULL;
		ssize_t count = scandir(plugins_path, &filelist, ann_plugins_file_filter, alphasort);
		if(filelist)
		{
			for(ssize_t i = 0; i < count; ++i)
			{
				char path_name[PATH_MAX] = "";
				snprintf(path_name, sizeof(path_name), "%s/%s", plugins_path, filelist[i]->d_name);
				free(filelist[i]);
				rc = helpler->add(helpler, path_name);
				if(rc)
				{
					fprintf(stderr, "[WARNING]::%s()::add '%s' failed\n", __FUNCTION__, path_name);
				}
			}
			free(filelist);
		}
	}
	return helpler->num_plugins;
}

static ann_plugins_helpler_t s_plugins[1] = {{
	.add 	= ann_plugins_helpler_add,
	.remove = ann_plugins_helpler_remove,
	.find 	= ann_plugins_helpler_find,
	.load 	= ann_plugins_helpler_load,
}};

ann_plugins_helpler_t * ann_plugins_helpler_get_default(void)
{
	return s_plugins;
}


ann_plugins_helpler_t * ann_plugins_helpler_init(ann_plugins_helpler_t * helpler, const char * plugins_path, void * user_data)
{
	if(NULL == helpler) helpler = s_plugins;

	helpler->user_data = user_data;
	helpler->add = ann_plugins_helpler_add;
	helpler->remove = ann_plugins_helpler_remove;
	helpler->find = ann_plugins_helpler_find;
	helpler->load = ann_plugins_helpler_load;

	ann_plugins_helpler_resize(helpler, 0);
	ssize_t count = helpler->load(helpler, plugins_path);
	if(count <= 0)
	{
		// log_warnings
	}
	return helpler;
}

void ann_plugins_helpler_cleanup(ann_plugins_helpler_t * helpler)
{
	if(helpler && helpler->plugins)
	{
		for(int i = 0; i < helpler->num_plugins; ++i)
		{
			ann_plugin_t * plugin = helpler->plugins[i];
			if(NULL == plugin) continue;
			helpler->plugins[i] = NULL;
			ann_plugin_free(plugin);
		}
		free(helpler->plugins);
		helpler->plugins = NULL;
	}
	helpler->num_plugins = 0;
	return;
}

ann_plugin_t * ann_plugin_new(const char * filename, void * user_data)
{
	void * handle = dlopen(filename, RTLD_LAZY | RTLD_LOCAL | RTLD_NODELETE);
	if(NULL == handle) return NULL;
	char * err_msg = NULL;
	dlerror();	// clear dll error status
	ann_plugin_get_type_function get_plugin_type = dlsym(handle, "ann_plugin_get_type");
	err_msg = dlerror();
	if(err_msg)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::dlsym(ann_plugin_get_type) failed: %s\n",
			__FUNCTION__, __LINE__,
			err_msg);
		dlclose(handle);
		return NULL;
	}

	const char * plugin_type = get_plugin_type();
	if(NULL == plugin_type)
	{
		fprintf(stderr, "[ERROR]::%s(%d)::get_plugin_type() failed: %s\n",
			__FUNCTION__, __LINE__,
			"unknown type");
		dlclose(handle);
		return NULL;
	}

	ann_plugin_t * plugin = calloc(1, sizeof(*plugin));
	assert(plugin);

	plugin->user_data = user_data;
	plugin->handle = handle;
	plugin->filename = strdup(filename);
	plugin->type = strdup(plugin_type);
	
	plugin->init_func 		= dlsym(handle, "ann_plugin_init");
	plugin->query_interface = dlsym(handle, "query_interface");

	err_msg = dlerror();		// clear error status
	(void)((err_msg));	// usused(rc)
	
	return plugin;
}

void ann_plugin_free(ann_plugin_t * plugin)
{
	if(NULL == plugin) return;

	free(plugin->filename); plugin->filename = NULL;
	free(plugin->type);		plugin->type = NULL;

	if(plugin->handle)
	{
		dlclose(plugin->handle);
		plugin->handle = NULL;
	}
	return;
}

#undef MAX_PLUGINS
