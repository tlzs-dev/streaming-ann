#ifndef _MJPEG_SERVER_H_
#define _MJPEG_SERVER_H_

#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "httpd.h"

/**
 * @ingroup dlp-framework
 * @{
 * 
 * @defgroup output-module Outputs
 * @{
 * @}
 */
 

typedef struct mjpg_server
{
	http_server_t http[1];
	void * priv;
	
	int (* run)(struct mjpg_server * mjpg, int async_mode);
	int (* stop)(struct mjpg_server * mjpg);
	int (* on_request)(struct mjpg_server * mjpg);		// can be overrided

	long frame_number;
	
	// push mode
	int (* update_jpeg)(struct mjpg_server * mjpg, const unsigned char * jpeg_data, size_t length);
	int (* update_bgra)(struct mjpg_server * mjpg, const unsigned char * bgra_data, int width, int height, int channels);

	// pull mode
	int (* need_data)(struct mjpg_server * mjpg);		// virtual callback, need override 
}mjpg_server_t;

mjpg_server_t * mjpg_server_init(mjpg_server_t * mjpg, const char * port, int local_only, void * user_data);
void mjpg_server_cleanup(mjpg_server_t * mjpg);

#ifdef __cplusplus
}
#endif
#endif

