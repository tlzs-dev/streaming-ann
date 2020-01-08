#!/bin/bash

TARGET=${1-"darknet"}

DARKNET_CFLAGS=" -Ithird-party/darknet/include -I. "
DARKNET_LIBS="third-party/darknet/libdarknet.a"

if [ -e /usr/local/cuda ] ; then
 DARKNET_CFLAGS+=" -DGPU -I/usr/local/cuda/include "
 DARKNET_LIBS+=" -L/usr/local/cuda/lib64 -lcudart -lcublas -lcurand "
fi

function build()
{
	local target=$1
	[ -z "${target}" ] && exit 1

	case "${target}" in
		darknet|darknet-wrapper):
			gcc -std=gnu99 -g -Wall -D_DEBUG -fPIC -shared -o plugins/libaiplugin-darknet.so \
				darknet.c darknet-wrapper.c -Iinclude -I. \
				utils/*.c \
				${DARKNET_CFLAGS} ${DARKNET_LIBS} \
				-lm -lpthread -ljson-c -ljpeg -lpng -lcairo \
				`pkg-config --cflags --libs gio-2.0 glib-2.0`
		;;
	*)
		exit 1
		;;
	esac
	return 0
}

build ${TARGET}
