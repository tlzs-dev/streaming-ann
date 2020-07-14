#!/bin/bash

if [ ! -d www/dataset ]; then
    sudo mkdir -p www/{dataset,tmp}
    sudo chown www-data:root www/{dataset,tmp}
fi
gcc -std=gnu99 -g -Wall -D_GNU_SOURCE -o upload upload.c `pkg-config --cflags --libs libsoup-2.4` -lm -lpthread -luuid -ljson-c
