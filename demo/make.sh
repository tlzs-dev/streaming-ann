#!/bin/bash

target=${1-"demo-01"}
target=${target/.[ch]/}

echo "build '${target}' ..."
case ${target} in
    demo-01)
        gcc -std=gnu99 -g -Wall -I../include  `pkg-config --cflags --libs gstreamer-1.0 gio-2.0 gtk+-3.0` \
            -o demo-01 demo-01.c da_panel.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -lcairo -ljpeg \
            -lcurl
        ;;
    ai-server)
        gcc -std=gnu99 -g -Wall -I../include  `pkg-config --cflags --libs libsoup-2.4` \
            -o ai-server ai-server.c \
            ../lib/libann-utils.a  \
            -lm -lpthread -ljson-c -ldl \
            -ljpeg \
            -lcairo
        ;;
    *)
        ;;
esac

