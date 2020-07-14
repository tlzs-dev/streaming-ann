/*
 * upload.c
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
#include <libsoup/soup.h>

#include <sys/types.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <pwd.h>
#include <errno.h>

#include <json-c/json.h>
#include <sys/stat.h>
#include <uuid/uuid.h>
#include <dirent.h>

typedef struct global_param
{
	char root_path[PATH_MAX];
	const char * data_path;
}global_param_t;

static global_param_t g_params[1] = {{
	.data_path = "dataset",
}};

static void on_reload_config(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data);
static void on_upload(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data);
static void on_get_file_list(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data);
int main(int argc, char **argv)
{
	global_param_t * params = g_params;
	ssize_t cb = readlink("/proc/self/exe", params->root_path, sizeof(params->root_path));
	assert(cb > 0);
	dirname(params->root_path);
	strcat(params->root_path, "/www");
	
	
	GError * gerr = NULL;
	SoupServer * server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "streamming-ann::uploader", NULL);
	
	soup_server_add_handler(server, "/dataset/reload-config", on_reload_config, params, NULL);
	soup_server_add_handler(server, "/dataset/upload", on_upload, params, NULL);
	soup_server_add_handler(server, "/dataset/getfilelist", on_get_file_list, params, NULL);
	soup_server_listen_all(server, 8080, 0, &gerr);
	if(gerr){
		fprintf(stderr, "soup_server_listen_all() failed: %s\n", gerr->message);
		g_error_free(gerr);
		return -1;
	}
	
	// runing under sandbox
	size_t buf_size = sysconf(_SC_GETPW_R_SIZE_MAX);
	if(buf_size == -1) buf_size = (1 << 16);
	char * buffer = calloc(1, buf_size);
	assert(buffer);
	
	struct passwd pwd;
	struct passwd * result = NULL;
	memset(&pwd, 0, sizeof(pwd));
	
	int rc = getpwnam_r("www-data", &pwd, buffer, buf_size, &result);
	assert(0 == rc && result);
	
	rc = chroot(params->root_path);
	assert(0 == rc);
	chdir("/");
	
	rc = setresgid(result->pw_gid, result->pw_gid, 0);
	assert(0 == rc);
	rc = setresuid(result->pw_uid, result->pw_uid, result->pw_uid);
	assert(0 == rc);
	free(buffer);
	
	// dump info
	GSList * uris = soup_server_get_uris(server);
	assert(uris);
	for(GSList * uri = uris; NULL != uri; uri = uri->next)
	{
		char * sz_uri = soup_uri_to_string(uri->data, FALSE);
		if(sz_uri) fprintf(stderr, "Listening on %s\n", sz_uri);
		soup_uri_free(uri->data);
		uri->data = NULL;
		free(sz_uri);
	}
	g_slist_free(uris);
	
	GMainLoop * loop = g_main_loop_new(NULL, 0);
	g_main_loop_run(loop);
	g_main_loop_unref(loop);
	
	return 0;
}



static inline ssize_t load_page(const char * page_file, char ** p_data) {
	struct stat st[1];
	memset(st, 0, sizeof(st));
	
	int rc = stat(page_file, st);
	if(rc) return -1;
	int mode = st->st_mode & S_IFMT;
	if(! (mode & S_IFREG)) return -1;
	
	ssize_t file_size = st->st_size;
	if(file_size <= 0) return -1;
	
	FILE * fp = fopen(page_file, "rb");
	assert(fp);
	char * data = malloc(file_size + 1);
	ssize_t cb = fread(data, 1, file_size, fp);
	assert(cb == file_size);
	data[cb] = '\0';
	
	fclose(fp);
	*p_data = data;
	return cb;
}

static char * s_upload_page = NULL;
static ssize_t s_upload_page_length = 0;

static void on_reload_config(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	if(s_upload_page) {
		free(s_upload_page);
		s_upload_page = NULL;
		s_upload_page_length = 0;
	}
	
	s_upload_page_length = load_page("upload.html", &s_upload_page);
	
	guint status = (s_upload_page_length > 0 && s_upload_page)?SOUP_STATUS_OK:SOUP_STATUS_INTERNAL_SERVER_ERROR;
	soup_message_set_status(msg, status);
	return;
}

static void on_upload(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	int rc = 0;
	global_param_t * params = user_data;
	assert(params && params->data_path);
	
	if(NULL == s_upload_page) {
		s_upload_page_length = load_page("upload.html", &s_upload_page);
	}
	assert(s_upload_page_length > 0 && s_upload_page);
	
	if(msg->method == SOUP_METHOD_GET)
	{
		soup_message_set_response(msg, "text/html;charset=utf-8", 
			SOUP_MEMORY_STATIC, 
			s_upload_page, s_upload_page_length);
		soup_message_set_status(msg, SOUP_STATUS_OK);
		return;
	}
	
	guint status = SOUP_STATUS_OK;
	static const char * status_ok_text = "<p style=\"color:green\">[OK]</p>";
	static const char * status_ng_text = "<p style=\"color:red\">[NG]</p>";
	
	if(msg->method == SOUP_METHOD_POST)
	{
		const char * content_type = soup_message_headers_get_content_type(msg->request_headers, NULL);
		long content_length = soup_message_headers_get_content_length(msg->request_headers);
		printf("content_type: %s, length=%ld\n", content_type, content_length);
	
		char tmp_filename[1024] = "/tmp/";
		char * filename = NULL;
		if(query) {
			filename = g_hash_table_lookup(query, "filename");
		}
		
		printf("path: %s, filename: %s\n", path, filename);
		uuid_t uid;
		uuid_generate(uid);
		
		uuid_unparse(uid, tmp_filename + strlen(tmp_filename));
		strcat(tmp_filename, ".tmp");
		
		FILE * fp = fopen(tmp_filename, "w+");
		if(fp)
		{
			assert(msg->request_body->length == content_length);
			ssize_t cb = fwrite(msg->request_body->data, 1, content_length, fp);
			fclose(fp);
			
			assert(cb == content_length);
		}
		
		if(filename) {
			char path_name[PATH_MAX] = "";
			snprintf(path_name, sizeof(path_name), "%s/%s", params->data_path, filename);
			rc = rename(tmp_filename, path_name);
			if(rc) status = SOUP_STATUS_INTERNAL_SERVER_ERROR;
		}
	}
	
	const char * text = (status==SOUP_STATUS_OK)?status_ok_text:status_ng_text;
	soup_message_set_response(msg, "text/html", SOUP_MEMORY_STATIC, text, strlen(text));
	soup_message_set_status(msg, status);
	return;
}

int regular_files_filter(const struct dirent * entry)
{
	return (entry->d_type == DT_REG);
}

static void on_get_file_list(SoupServer * server, SoupMessage * msg, const char * path, 
	GHashTable * query, SoupClientContext * client, gpointer user_data)
{
	global_param_t * params = user_data;
	fprintf(stderr, "%s(): method=%s\n", __FUNCTION__, msg->method);
	if(msg->method == SOUP_METHOD_GET) {
		struct dirent ** file_list = NULL;
		int count = scandir(params->data_path, &file_list, regular_files_filter, versionsort);
		json_object * jlist = json_object_new_object();
		json_object_object_add(jlist, "path", json_object_new_string(params->data_path));
		
		json_object * jfiles = json_object_new_array();
		json_object_object_add(jlist, "files", jfiles);
		
		if(file_list)
		{
			for(int i = 0; i < count; ++i) {
				json_object_array_add(jfiles, json_object_new_string(file_list[i]->d_name));
				free(file_list[i]);
			}
			free(file_list);
		}
		const char * response = json_object_to_json_string_ext(jlist, JSON_C_TO_STRING_PLAIN);
		soup_message_set_response(msg, "application/json", SOUP_MEMORY_COPY, response, strlen(response));
		soup_message_set_status(msg, SOUP_STATUS_OK);
		json_object_put(jlist);
		
		return;
	}else if(msg->method == SOUP_METHOD_OPTIONS) {// add CORS support 
		SoupMessageHeaders * resp_hdrs = msg->response_headers;
		soup_message_headers_append(resp_hdrs, "Access-Control-Allow-Origin", "*");
		soup_message_headers_append(resp_hdrs, "Access-Control-Request-Method", "GET");
		soup_message_headers_append(resp_hdrs, "Access-Control-Request-Headers", "Content-Type, Content-Length");
		soup_message_headers_append(resp_hdrs, "Access-Control-Max-Age", "86400");
		soup_message_set_status(msg, SOUP_STATUS_NO_CONTENT);
		return;
	}
	
	soup_message_set_status(msg, SOUP_STATUS_BAD_REQUEST);
	return;
}

