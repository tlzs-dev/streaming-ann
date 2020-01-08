#ifndef _IMG_PROC_H_
#define _IMG_PROC_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @ingroup img_proc
 * @{
 */
typedef struct bgra_image
{
	unsigned char * data;
	int width;
	int height;
	int channels;
	int stride;
}bgra_image_t;
bgra_image_t * bgra_image_init(bgra_image_t * image, int width, int height, const unsigned char * image_data);
void bgra_image_clear(bgra_image_t * image);
/**
 * @}
 */


/**
 * @ingroup img_proc
 * @{
 */
int bgra_image_from_jpeg_stream(bgra_image_t * image, const unsigned char * jpeg, size_t length);
int bgra_image_from_png_stream(bgra_image_t * image, const unsigned char * jpeg, size_t length);
int bgra_image_load_data(bgra_image_t * image, const void * image_data, size_t size);		// image_data: png or jpeg format
int bgra_image_load_from_file(bgra_image_t * image, const char * filename);
int bgra_image_save_to_file(bgra_image_t * image, const char * filename, int quality);	// quality(0 ~ 100): for jpeg only, default 95
int bgra_image_save_to_jpeg(bgra_image_t * image, const char * filename, int quality);
int bgra_image_save_to_png(bgra_image_t * image, const char * filename);
ssize_t bgra_image_to_jpeg_stream(bgra_image_t * image, unsigned char ** jpeg_stream, int quality);
ssize_t bgra_image_to_png_stream(bgra_image_t * image, unsigned char ** png_stream);

int img_utils_get_jpeg_size(const unsigned char * jpeg, size_t length, int * p_width, int * p_height);
int img_utils_get_png_size(const unsigned char * png, size_t length, int * p_width, int * p_height);

/**
* @}
*/
#ifdef __cplusplus
}
#endif
#endif

