#!/bin/bash

target=${1-"demo-01"}
target=${target/.[ch]/}

if [[ "$OS" == "Windows_NT" ]] ; then
CFLAGS+=" -D_WIN32 -IC:/msys64/mingw64/include "
fi

echo "build '${target}' ..."
case ${target} in
    demo-01|demo-02|demo-03|config-tools|demo-01-win)
        gcc -std=gnu99 -g -Wall -I../include  \
            -o ${target} ${target}.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            `pkg-config --cflags --libs gstreamer-1.0 glib-2.0 gio-2.0 gtk+-3.0 libcurl` 
        ;;
    ai-server)
        gcc -std=gnu99 -O3 -Wall -I../include  `pkg-config --cflags --libs libsoup-2.4` \
            -o ai-server ai-server.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -ljpeg \
            -lcairo
        ;;
    *)
        ;;
esac

