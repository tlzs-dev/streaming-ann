/*
 * img_proc.c
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

#include "img_proc.h"
#include <cairo/cairo.h>

bgra_image_t * bgra_image_init(bgra_image_t * image, int width, int height, const unsigned char * image_data)
{
	if(width < 1 || height < 1) return NULL;
	
	if(NULL == image) image = calloc(1, sizeof(*image));
	assert(image);
	static const int channels = 4;
	
	ssize_t size = width * height * channels;
	assert(size > 0);

	unsigned char * data = realloc(image->data, size);
	assert(data);

	image->data = data;
	image->width = width;
	image->height = height;
	image->channels = channels;

	if(image_data)
	{
		memcpy(data, image_data, size);
	}
	
	return image;
	
}
void bgra_image_clear(bgra_image_t * image)
{
	if(NULL == image) return;
	free(image->data);
	memset(image, 0, sizeof(*image));
	return;
}



#include <jpeglib.h>
#include <cairo/cairo.h>
#include <glib.h>
#include <gio/gio.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <setjmp.h>

enum image_type
{
	image_type_unknown,
	image_type_png,
	image_type_jpeg,
};

// guess mime-type byfilename or image_data
static inline enum image_type guess_image_type(const char * filename, const unsigned char * image_data, size_t size)
{
	gboolean uncertain = TRUE;
	gchar * mime_type = NULL; 
	enum image_type type = image_type_unknown;
	mime_type = g_content_type_guess(filename, image_data, size, &uncertain);
	
	if(!uncertain && mime_type)
	{
		if(strcasecmp(mime_type, "image/jpeg") == 0) type = image_type_jpeg;
		else if(strcasecmp(mime_type, "image/png") == 0) type = image_type_png;
		else
		{
			fprintf(stderr, "unsupported image type: %s\n", mime_type);
		}
	}else
	{
		fprintf(stderr, "unknown image type: filename=%s\n", filename);
	}
	if(mime_type) g_free(mime_type);
	return type;
}


typedef struct custom_jpeg_err
{
	struct jpeg_error_mgr base[1];
	jmp_buf setjmp_buffer;
}custom_jpeg_err_t;

static void on_jpeg_decompress_error(j_common_ptr cinfo)
{
	custom_jpeg_err_t * jerr = (custom_jpeg_err_t *)cinfo->err;
	assert(jerr);

	cinfo->err->output_message(cinfo);
	longjmp(jerr->setjmp_buffer, 1);
}

int img_utils_get_jpeg_size(const unsigned char * jpeg, size_t length, int * p_width, int * p_height)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	memset(&cinfo, 0, sizeof(cinfo));
	memset(&jerr, 0, sizeof(jerr));

	cinfo.err = jpeg_std_error(&jerr);
	
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, jpeg, length);
	int width = 0;
	int height = 0;
	
	int rc = jpeg_read_header(&cinfo, FALSE);
	if(rc != JPEG_HEADER_OK) return -1;

	width = cinfo.image_width;
	height = cinfo.image_height;

	if(p_width) *p_width = width;
	if(p_height) * p_height = height;

	jpeg_destroy_decompress(&cinfo);
	
	return 0;
}

int bgra_image_from_jpeg_stream(bgra_image_t * image, const unsigned char * jpeg, size_t length)
{
	int rc = -1;
	struct jpeg_decompress_struct cinfo;
	memset(&cinfo, 0, sizeof(cinfo));
	
	custom_jpeg_err_t jerr;
	memset(&jerr, 0, sizeof(jerr));
	
	cinfo.err = jpeg_std_error((struct jpeg_error_mgr *)&jerr);
	jerr.base->error_exit = on_jpeg_decompress_error;
	if(setjmp(jerr.setjmp_buffer))
	{
		goto label_cleanup;
	}
	
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, jpeg, length);
	int width = 0;
	int height = 0;
	
	(void)jpeg_read_header(&cinfo, TRUE);
	
	cinfo.out_color_space = JCS_EXT_BGRA;
	(void)jpeg_start_decompress(&cinfo);
	
	printf("dst size: %d x %d, channels=%d color_space=%d\n", 
		cinfo.output_width, cinfo.output_height, 
		cinfo.output_components,
		cinfo.out_color_space);

	width = cinfo.output_width;
	height = cinfo.output_height;
	printf("-->image size: %d x %d\n", width, height);
	
	
	assert(cinfo.out_color_space == JCS_EXT_BGRA);
	image = bgra_image_init(image, width, height, NULL);
	assert(image);
	
	unsigned char * row = image->data;
	int row_stride = image->width * 4;
	JSAMPLE * row_pointer[1];
	memset(row_pointer, 0, sizeof(row_pointer));
	while(cinfo.output_scanline < cinfo.output_height)
	{
		row_pointer[0] = (JSAMPLE *)row;
		int n = jpeg_read_scanlines(&cinfo, row_pointer, 1);
		assert(n == 1);
		
		row += row_stride;
	}
	
	rc = 0;
label_cleanup:
	jpeg_finish_decompress(&cinfo);

	jpeg_destroy_decompress(&cinfo);
	return rc;

}

typedef struct png_closure
{
	unsigned char * iter;
	unsigned int bytes_left;
}png_closure_t;

cairo_status_t on_read_png_stream(png_closure_t * closure, unsigned char * data, unsigned int length)
{
	if(length > closure->bytes_left) return CAIRO_STATUS_READ_ERROR;
	
	memcpy(data, closure->iter, length);
	closure->iter += length;
	closure->bytes_left -= length;
	return CAIRO_STATUS_SUCCESS;
}

int bgra_image_from_png_stream(bgra_image_t * image, const unsigned char * png, size_t length)
{
	int rc = -1;
	png_closure_t closure[1] = {{
		.iter = (unsigned char *)png,
		.bytes_left = length,
	}};
	cairo_surface_t * surface = cairo_image_surface_create_from_png_stream(
		(cairo_read_func_t)on_read_png_stream,
		closure);
	
	if(surface) {
		unsigned char * image_data = cairo_image_surface_get_data(surface);
		int width = cairo_image_surface_get_width(surface);
		int height = cairo_image_surface_get_height(surface);
		
		image = bgra_image_init(image, width, height, image_data);
		if(image != NULL) rc = 0;
	}
	if(surface) cairo_surface_destroy(surface);
	return rc;
}

int img_utils_get_png_size(const unsigned char * png, size_t length, int * p_width, int * p_height)
{
	bgra_image_t bgra[1];
	memset(bgra, 0, sizeof(bgra));
	int rc = bgra_image_from_png_stream(bgra, png, length);
	if(rc) return -1;

	if(p_width) *p_width = bgra->width;
	if(p_height) * p_height = bgra->height;

	bgra_image_clear(bgra);
	return 0;
}


int bgra_image_load_data(bgra_image_t * image, 
	const void * image_data, // image_data: png or jpeg format
	size_t length)
{
	enum image_type type = guess_image_type(NULL, image_data, length);
	switch(type)
	{
	case image_type_jpeg: bgra_image_from_jpeg_stream(image, image_data, length); break;
	case image_type_png: bgra_image_from_png_stream(image, image_data, length); break;
	default:
		fprintf(stderr, "[WARNING]::%s()::unable to load image! (UNKNOWN TYPE)\n", __FUNCTION__);
		return -1;
	}
	return 0;
}


int bgra_image_load_from_file(bgra_image_t * image, const char * filename)
{
	int rc = 0;
	struct stat st[1];
	memset(st, 0, sizeof(st));
	rc = stat(filename, st);
	if(rc)
	{
		int err = errno;
		fprintf(stderr, "[WARNING]::%s(%s):%s\n",
			__FUNCTION__, filename,
			strerror(err));
		return -1;
	}

	if(! S_ISREG(st->st_mode))
	{
		fprintf(stderr, "[WARNING]::%s(%s):NOT regular file\n",
			__FUNCTION__, filename);
		return -1;
	}
	
	ssize_t length = st->st_size;
	
	FILE * fp = fopen(filename, "rb");
	if(NULL == fp)
	{
		int err = errno;
		fprintf(stderr, "[WARNING]::%s(%s)::%s\n", __FUNCTION__, filename, strerror(err));
		return -1;
	}
	rc = -1;
	if(length > 0)
	{
		unsigned char * data = malloc(length);
		assert(data);
		
		ssize_t cb = fread(data, 1, length, fp);
		assert(cb == length);
		fclose(fp);
		fp = NULL;
		
		rc = bgra_image_load_data(image, data, length);
		free(data);
	}
	return rc;
}


ssize_t bgra_image_to_jpeg_stream(bgra_image_t * image, unsigned char ** jpeg_stream, int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	memset(&jerr, 0, sizeof(jerr));
	
	memset(&cinfo, 0, sizeof(cinfo));
	
	int rc = 0;
	unsigned long cb_jpeg = 0;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, jpeg_stream, &cb_jpeg);
	
	cinfo.image_width = image->width;
	cinfo.image_height = image->height;
	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_EXT_BGRA;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	
	jpeg_start_compress(&cinfo, TRUE);
	JSAMPROW row_pointer[1] = { NULL };
	
	unsigned char * row = image->data;
	int row_stride = image->stride?image->stride:(image->width * 4);
	while(cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer[0] = (JSAMPLE *)row;
		int n = jpeg_write_scanlines(&cinfo, row_pointer, 1);
		if(n != 1) { rc = -1; break; }
		row += row_stride;
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	if(rc) return 0;
	
	return cb_jpeg;
}
ssize_t bgra_image_to_png_stream(bgra_image_t * image, unsigned char ** png_stream)
{
	
	return 0;
}

int bgra_image_save_to_file(bgra_image_t * image, const char * filename, int quality)	// quality(0 ~ 100): for jpeg only, default 95
{
	enum image_type type = guess_image_type(filename, NULL, 0);
	if(type == image_type_unknown)
	{
		type = image_type_png;		// default --> save as png file
	}
	
	if(type == image_type_png)
	{
		bgra_image_save_to_png(image, filename);
	}else if(type == image_type_jpeg)
	{
		bgra_image_save_to_jpeg(image, filename, quality);
	}else
	{
		fprintf(stderr, "[WARNING]::%s(%s)::unable to save image! (UNKNOWN TYPE)\n", __FUNCTION__, filename);
		return -1;
	}
	return 0;
}

int bgra_image_save_to_jpeg(bgra_image_t * image, const char * filename, int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	
	int rc = 0;
	memset(&cinfo, 0, sizeof(cinfo));
	
	FILE * fp = fopen(filename, "wb");
	if(NULL == fp)
	{
		int err = errno;
		fprintf(stderr, "[WARNING]::%s(%s)::%s\n", __FUNCTION__, filename, strerror(err));
		return err;
	}
	
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, fp);
	
	cinfo.image_width = image->width;
	cinfo.image_height = image->height;
	cinfo.input_components = 4;
	cinfo.in_color_space = JCS_EXT_BGRA;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	
	jpeg_start_compress(&cinfo, TRUE);
	JSAMPROW row_pointer[1] = { NULL };
	
	unsigned char * row = image->data;
	int row_stride = image->stride?image->stride:(image->width * 4);
	while(cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer[0] = (JSAMPLE *)row;
		int n = jpeg_write_scanlines(&cinfo, row_pointer, 1);
		if(n != 1) { rc = -1; break;  }
		row += row_stride;
	}
	jpeg_finish_compress(&cinfo);
	fclose(fp);
	jpeg_destroy_compress(&cinfo);
	return rc;
}

int bgra_image_save_to_png(bgra_image_t * image, const char * filename)
{
	int rc = -1;
	cairo_surface_t * png = cairo_image_surface_create_for_data(image->data,
		CAIRO_FORMAT_ARGB32,
		image->width, image->height,
		image->stride?image->stride:(image->width * 4));
	if(png && cairo_surface_status(png) == CAIRO_STATUS_SUCCESS)
	{
		rc = cairo_surface_write_to_png(png, filename);
	}
	if(png) cairo_surface_destroy(png);
	return rc;
}
