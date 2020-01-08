#!/bin/bash
gcc -std=gnu99 -g -Wall -o webserver webserver.c lib/libann-utils.a -I ../../include `pkg-config --libs --cflags libsoup-2.4 gio-2.0 glib-2.0` -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl

