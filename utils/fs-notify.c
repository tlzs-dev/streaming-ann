/*
 * fs-notify.c
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

#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include "fs-notify.h"

#include <pthread.h>
#include <sys/stat.h>

#include "utils.h"

static const int s_default_watch_flags = IN_ACCESS
	| IN_ATTRIB
	| IN_CLOSE_WRITE
	| IN_CLOSE_NOWRITE
	| IN_CREATE
	| IN_DELETE
	| IN_DELETE_SELF
	| IN_MODIFY
	| IN_MOVE_SELF
	| IN_MOVED_FROM
	| IN_MOVED_TO
	| IN_OPEN
//	| IN_DONT_FOLLOW
	| 0;


fsnotify_data_t * fsnotify_data_new(int ifd, const char * path_name, int flags, void * user_data)
{
	if(NULL == path_name || !path_name[0]) return NULL;

	struct stat st[1];
	int rc = 0;

	rc = stat(path_name, st);
	if(rc != 0 || (!S_ISREG(st->st_mode) && !S_ISDIR(st->st_mode)))
	{
		fprintf(stderr, "[ERROR]::%s()::invalid watch_path: [%s]\n", __FUNCTION__, path_name);
		return NULL;
	}
	
	if(flags < 0) flags = s_default_watch_flags;

	int wd = inotify_add_watch(ifd, path_name, flags);
	if(wd <=  0) return NULL;

	fsnotify_data_t * notify_data = calloc(1, sizeof(*notify_data));
	assert(notify_data);

	notify_data->user_data = user_data;
	strncpy(notify_data->path_name, path_name, sizeof(notify_data->path_name));

	notify_data->ifd = ifd;
	notify_data->wd = wd;
	notify_data->flags = flags;
	
	return notify_data;
}

void fsnotify_data_free(fsnotify_data_t * notify_data)
{
	if(NULL == notify_data) return;

	if(notify_data->ifd > 0 && notify_data->wd > 0)
	{
		inotify_rm_watch(notify_data->ifd, notify_data->wd);
		notify_data->wd = -1;
	}
	free(notify_data);
	return ;
}


typedef struct filesystem_notify_private
{
	filesystem_notify_t * fsnotify;
	int ifd;		// inotify fd
	
	fsnotify_data_t ** notify_data;
	ssize_t max_size;
	ssize_t count;

	pthread_mutex_t mutex;
	pthread_t th;

	int async_mode;
	
}filesystem_notify_private_t;

#define FILESYSTEM_NOTIFY_ALLOCATION_SIZE	(256)
int filesystem_notify_private_resize(filesystem_notify_private_t * priv, ssize_t new_size)
{
	if(0 == new_size) new_size = FILESYSTEM_NOTIFY_ALLOCATION_SIZE;
	else new_size = (new_size + FILESYSTEM_NOTIFY_ALLOCATION_SIZE - 1) / FILESYSTEM_NOTIFY_ALLOCATION_SIZE * FILESYSTEM_NOTIFY_ALLOCATION_SIZE;

	if(new_size <= priv->max_size) return 0;

	fsnotify_data_t ** notify_data = realloc(priv->notify_data, new_size * sizeof(*notify_data));
	assert(notify_data);

	memset(notify_data, 0, (new_size - priv->max_size) * sizeof(*notify_data));
	priv->max_size = new_size;
	priv->notify_data = notify_data;

	return 0;
}

static filesystem_notify_private_t * filesystem_notify_private_new(filesystem_notify_t * fsnotify)
{
	filesystem_notify_private_t * priv = calloc(1, sizeof(*priv));
	assert(priv);
	priv->fsnotify = fsnotify;
	fsnotify->priv = priv;


	int ifd = inotify_init1(IN_NONBLOCK);
	assert(ifd != -1);

	priv->ifd = ifd;

	pthread_mutex_init(&priv->mutex, NULL);

	int rc = filesystem_notify_private_resize(priv, 0);
	assert(0 == rc);

	return priv;
}

static void filesystem_notify_private_cleanup(filesystem_notify_private_t * priv)
{
	if(NULL == priv) return;

	pthread_mutex_lock(&priv->mutex);
	if(priv->notify_data)
	{
		for(ssize_t i = 0; i < priv->count; ++i)
		{
			fsnotify_data_t * notify_data = priv->notify_data[i];
			if(notify_data)
			{
				fsnotify_data_free(notify_data);
				priv->notify_data[i] = NULL;
			}
		}
	}
	priv->count = 0;
	free(priv->notify_data);
	priv->notify_data = NULL;


	priv->fsnotify->quit = 1;

	if(priv->ifd > 0)
	{
		close(priv->ifd);
		priv->ifd = -1;
	}

	pthread_mutex_unlock(&priv->mutex);

	if(priv->th)
	{
		void * exit_code = NULL;
		int rc = pthread_join(priv->th, &exit_code);
		UNUSED(rc);

		debug_printf("%s()::thread exited with code %ld, rc = %d\n", __FUNCTION__, (long)exit_code, rc);
	}
	pthread_mutex_destroy(&priv->mutex);
	free(priv);
	return;
}

static int filesystem_notify_add_watch(struct filesystem_notify * fsnotify, const char * path, int flags)
{
	filesystem_notify_private_t * priv = fsnotify->priv;
	assert(priv);
	
	fsnotify_data_t * notify_data = fsnotify_data_new(priv->ifd, path, flags, fsnotify);
	if(NULL == notify_data) return -1;

	pthread_mutex_lock(&priv->mutex);
	int rc = filesystem_notify_private_resize(priv, priv->count + 1);
	assert((0 == rc) && priv->count >= 0 && priv->count < priv->max_size);
	priv->notify_data[priv->count++] = notify_data;
	pthread_mutex_unlock(&priv->mutex);
	return 0;
}

static int filesystem_notify_remove_watch(struct filesystem_notify * fsnotify, int index)
{
	filesystem_notify_private_t * priv = fsnotify->priv;
	assert(priv);
	if(index < 0 || index >= priv->count) return -1;

	pthread_mutex_lock(&priv->mutex);
	fsnotify_data_t * notify_data = priv->notify_data[index];
	priv->notify_data[index] = NULL;

	--priv->count;
	if(priv->count != index) priv->notify_data[index] = priv->notify_data[priv->count];

	pthread_mutex_unlock(&priv->mutex);
	if(notify_data) fsnotify_data_free(notify_data);

	return 0;
}
static int filesystem_notify_update_watch(struct filesystem_notify * fsnotify, int index, const char * path, int flags)
{
	filesystem_notify_private_t * priv = fsnotify->priv;
	assert(priv);
	if(index < 0 || index >= priv->count) return -1;
	if(flags <= 0) flags = s_default_watch_flags;
	
//	pthread_mutex_lock(&priv->mutex);
	fsnotify_data_t * notify_data = priv->notify_data[index];
	priv->notify_data[index] = NULL;

	if(notify_data)	fsnotify_data_free(notify_data);
	notify_data = fsnotify_data_new(priv->ifd, path, flags, fsnotify);

	priv->notify_data[index] = notify_data;
//	pthread_mutex_unlock(&priv->mutex);

	return (notify_data?0:-1);
}

static int filesystem_notify_on_notify(struct filesystem_notify * fsnotify, const fsnotify_data_t * notify_data)	// default callback
{
	debug_printf("%s()::path=%s, event=0x%.8x, is_folder=%d, filename=%s\n",
		__FUNCTION__,
		notify_data->path_name,
		notify_data->event->mask,
		(notify_data->event->mask & IN_ISDIR)?1:0,
		notify_data->event->len?notify_data->event->name:"(null)");
	return 0;
}

static void * filesystem_notify_process(void * user_data)
{
	int rc = 0;
	struct filesystem_notify * fsnotify = user_data;
	assert(fsnotify && fsnotify->priv);
	filesystem_notify_private_t * priv = fsnotify->priv;

	char buffer[8192] __attribute__((aligned(__alignof__(struct inotify_event))));
	memset(buffer, 0, sizeof(buffer));

	const struct inotify_event * event;

	struct pollfd pfd[1];
	memset(pfd, 0, sizeof(pfd));

	

	int timeout = 1000;
	rc = 0;
	while(0 == rc && !fsnotify->quit && priv->ifd)
	{
		int ifd = priv->ifd;

		pfd[0].fd = priv->ifd;
		pfd[0].events = POLLIN | POLLHUP | POLLPRI;
	
		int n = poll(pfd, 1, timeout);

		if(fsnotify->quit && priv->ifd <= 0) break;
		if(n == 0) continue;	// timeout
		if(n < 0)
		{
			if(errno == EINTR) continue;
			rc = errno;
			perror("poll");
			break;
		}
		ssize_t cb = read(ifd, buffer, sizeof(buffer));
		if(cb <= 0)
		{
			if(cb < 0 && errno != EAGAIN)
			{
				perror("read");	// invalid ifd
				rc = errno;
				break;
			}
			continue;
		}

		pthread_mutex_lock(&priv->mutex);
		char * p = buffer;
		char * p_end = buffer + cb;

		while(p < p_end)
		{
			event = (const struct inotify_event *)p;
			for(ssize_t i = 0; i < priv->count; ++i)
			{
			//	printf("i=%d\n", (int)i);
				fsnotify_data_t * notify_data = priv->notify_data[i];
				if(fsnotify->on_notify && notify_data && notify_data->wd == event->wd)
				{
					notify_data->event = event;
					fsnotify->on_notify(fsnotify, notify_data);
					break;
				}
			}
			p += sizeof(struct inotify_event) + event->len;
			
		}
		pthread_mutex_unlock(&priv->mutex);
	}
	if(priv->async_mode) pthread_exit((void *)(long)rc);

	return (void *)(long)rc;
}

static int filesystem_notify_run(struct filesystem_notify * fsnotify, int async_mode)
{
	filesystem_notify_private_t * priv = fsnotify->priv;
	priv->async_mode = async_mode;

	int rc = 0;
	if(async_mode)
	{
		rc = pthread_create(&priv->th, NULL, filesystem_notify_process, fsnotify);
		assert(0 == rc);
	}else
	{
		rc = (int)(long)filesystem_notify_process(fsnotify);
	}
	return rc;
	
}
static int filesystem_notify_stop(struct filesystem_notify * fsnotify)
{
	fsnotify->quit = 1;
	return 0;
}

filesystem_notify_t * filesystem_notify_init(filesystem_notify_t * fsnotify, void * user_data)
{
	if(NULL == fsnotify) fsnotify = calloc(1, sizeof(*fsnotify));
	assert(fsnotify);
	
	fsnotify->user_data = user_data;

	fsnotify->add_watch = filesystem_notify_add_watch;
	fsnotify->remove_watch = filesystem_notify_remove_watch;
	fsnotify->update_watch = filesystem_notify_update_watch;
	fsnotify->on_notify = filesystem_notify_on_notify;

	fsnotify->run = filesystem_notify_run;
	fsnotify->stop = filesystem_notify_stop;
	
	fsnotify->get_data = filesystem_notify_get_data;

	filesystem_notify_private_t * priv = filesystem_notify_private_new(fsnotify);
	assert(priv && priv->fsnotify == fsnotify && fsnotify->priv == priv);
	
	return fsnotify;
}

void filesystem_notify_cleanup(filesystem_notify_t * fsnotify)
{
	if(NULL == fsnotify) return;
	filesystem_notify_private_cleanup(fsnotify->priv);
	fsnotify->priv = NULL;

	fsnotify->quit = 1;

	
	
	
	return;
	
}


fsnotify_data_t * filesystem_notify_get_data(filesystem_notify_t * fsnotify, int index)
{
	filesystem_notify_private_t * priv = fsnotify->priv;
	assert(priv);
	
	int count = priv->count;
	if(index < 0 || index >= count) return NULL;
	
	return priv->notify_data[index];
}


#if defined(_TEST_FS_NOTIFY) && defined(_STAND_ALONE)
int main(int argc, char ** argv)
{
	filesystem_notify_t * fsnotify = filesystem_notify_init(NULL, NULL);
	assert(fsnotify);

	int rc = fsnotify->add_watch(fsnotify, "input", IN_CREATE | IN_MOVED_TO);
	rc = fsnotify->add_watch(fsnotify, "input/json", IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVE_SELF | IN_DELETE_SELF);
	assert(0 == rc);

	fsnotify->run(fsnotify, 1);


	char buf[200] = "";
	char * line = NULL;

	while((line = fgets(buf, sizeof(buf), stdin)))
	{
		if(line[0] == 'q') break;
	}

	fsnotify->stop(fsnotify);
	
	filesystem_notify_cleanup(fsnotify);
	free(fsnotify);
	
	return 0;
}
#endif



