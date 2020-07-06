#ifndef _DA_PANEL_H_
#define _DA_PANEL_H_

#include <stdio.h>
#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct da_panel da_panel_t;
struct da_panel
{
	void * shell;
	GtkWidget * frame;
	GtkWidget * da;
	int width, height;	// widget size
	
	cairo_surface_t * surface;
	unsigned char * image_data;
	int image_width;
	int image_height;
	
	double x_offset;
	double y_offset;
	
	void (* clear)(struct da_panel * panel);
};
struct da_panel * da_panel_init(struct da_panel * panel, int image_width, int image_height, void * shell);
void da_panel_cleanup(struct da_panel * panel);

#ifdef __cplusplus
}
#endif
#endif
