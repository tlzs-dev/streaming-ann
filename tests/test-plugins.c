/*
 * test-plugins.c
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

#include "ann-plugin.h"

void ann_plugins_dump(ann_plugins_helpler_t * helpler);

int main(int argc, char **argv)
{
	const char * plugins_path = "plugins";
	if(argc > 1) plugins_path = argv[1];
	
	ann_plugins_helpler_t * helpler = ann_plugins_helpler_init(NULL, plugins_path, NULL);
	ann_plugins_dump(helpler);

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
