/*
 * test-imgproc.c
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

#include "img_proc.h"

int main(int argc, char **argv)
{
	bgra_image_t bgra[1];
	memset(bgra, 0, sizeof(bgra));
	
	bgra_image_load_from_file(bgra, "1.png");
	assert(bgra->data && bgra->width > 0 && bgra->height);
	
	unsigned char * jpeg = NULL;
	ssize_t cb_jpeg = 0;
	
	cb_jpeg = bgra_image_to_jpeg_stream(bgra, &jpeg, 90);
	assert(cb_jpeg > 0 && jpeg);
	
	FILE * fp = fopen("result.jpg", "wb+");
	assert(fp);
	
	ssize_t cb = fwrite(jpeg, 1, cb_jpeg, fp);
	assert(cb == cb_jpeg);
	
	fclose(fp);
	
	free(jpeg);
	bgra_image_clear(bgra);
	
	return 0;
}

