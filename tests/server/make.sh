#!/bin/bash


gcc -std=gnu99 -g -Wall -D_DEBUG -g -Wall -o test-ai-server test-ai-server.c ../../lib/libann-utils.a -lm -lpthread -ldl `pkg-config --cflags --libs gio-2.0 glib-2.0` -ljson-c -lpng -ljpeg -lcairo `pkg-config --cflags --libs gstreamer-1.0 gtk+-3.0` -lcurl -I ../../include -DGPU

exit 0

## not used
CFLAGS=`pkg-config --cflags glib-2.0 gio-2.0 gstreamer-1.0`

# mjpeg-server.o
gcc -std=gnu99 -g -Wall -D_GNU_SOURCE -o mjpeg-server.o -c mjpeg-server.c -I../../include ${CFLAGS}


# httpd.o
gcc -std=gnu99 -g -Wall -D_GNU_SOURCE  -o httpd.o -c httpd.c -I../../include ${CFLAGS}


# ai-server.o
gcc -std=gnu99 -g -Wall -D_GNU_SOURCE  -o ai-server.o -c ai-server.c -I../../include ${CFLAGS}


# linker
gcc -std=gnu99 -g -Wall -o ai-server ai-server.o httpd.o mjpeg-server.o \
	../../lib/libann-utils.a \
	-lm -lpthread -ljson-c -ldl \
	-ljpeg -lpng -lcairo \
	-I ../../include -D_DEBUG -D_GNU_SOURCE \
	`pkg-config --cflags --libs gio-2.0 glib-2.0 gstreamer-1.0`


