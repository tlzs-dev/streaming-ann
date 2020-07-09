#ifndef _FS_NOTIFY_H_
#define _FS_NOTIFY_H_

#if !defined(_WIN32) && ! defined(WIN32)

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdint.h>

#include <sys/inotify.h>

typedef struct fsnotify_data
{
	void * user_data;				// same as fsnotify->user_data
	char path_name[PATH_MAX];
	int ifd;						// inotify fd
	int wd;							// watch fd
	int flags;						// watch flags

	int is_deleted;
	int is_moved;

	const struct inotify_event * event;
	//~ const struct					/* struct inotify_event */
	//~ {
		//~ int wd;
		//~ uint32_t mask;
		//~ uint32_t cookie;
		//~ uint32_t len;
		//~ char name[0];
	//~ } * event;						
}fsnotify_data_t;

fsnotify_data_t * fsnotify_data_new(int ifd, const char * path_name, int flags, void * user_data);
void fsnotify_data_free(fsnotify_data_t * notify_data);


typedef struct filesystem_notify
{
	void * user_data;
	void * priv;

	int quit;
	int paused;

	int (* add_watch)(struct filesystem_notify * fsnotify, const char * path, int flags);
	int (* remove_watch)(struct filesystem_notify * fsnotify, int index);
	int (* update_watch)(struct filesystem_notify * fsnotify, int index, const char * path, int flags);

	int (* run)(struct filesystem_notify * fsnotify, int async_mode);
	int (* stop)(struct filesystem_notify * fsnotify);

	int (* on_notify)(struct filesystem_notify * fsnotify, const fsnotify_data_t * notify_data);
	
	fsnotify_data_t * (* get_data)(struct filesystem_notify * fsnotify, int index);
}filesystem_notify_t;

filesystem_notify_t * filesystem_notify_init(filesystem_notify_t * fsnotify, void * user_data);
void filesystem_notify_cleanup(filesystem_notify_t * fsnotify);

fsnotify_data_t * filesystem_notify_get_data(filesystem_notify_t * fsnotify, int index);

#ifdef __cplusplus
}
#endif

#endif
#endif

