#!/bin/bash

TARGET=${1-"plugins-helpler"}

## remove extension_name
TARGET=${TARGET/.[ch]/}
TARGET=${TARGET/.so*/}


TARGETS=(
 "input-source"
 "tcp-server"
 "http-server"
 "http-client"
)

CC="gcc -std=gnu99 -D_GNU_SOURCE "
CFLAGS="-Wall -Iinclude"

if [ ! -z "${DEBUG}"  ]; then
CFLAGS+=" -g -D_DEBUG "
fi

if [[ "$OS" == "Windows_NT" ]] ; then
CFLAGS+=" -D_WIN32 -IC:/msys64/mingw64/include "
fi

echo "cflags: ${CFLAGS}"

echo "build ${TARGET} ..."


function build()
{
	local target=$1
	echo "build ${target} ..."
	
	case "${target}" in
		input-source|default-plugin)
			${CC} -fPIC -shared -o plugins/libioplugin-default.so \
				default-plugin.c input-source.c \
				${CFLAGS}	\
				utils/*.c \
				-lm -lpthread -ljpeg -lpng \
				`pkg-config --cflags --libs gstreamer-1.0 gio-2.0 glib-2.0 cairo json-c` 
			;;
		tcp-server)
			echo "make libioplugin-tcpd ..."
			${CC} -fPIC -shared -o plugins/libioplugin-tcpd.so \
				tcp-server.c \
				${CFLAGS}	-ljpeg -lpng \
				-lpthread -lm  `pkg-config --libs json-c cairo`
			;;
		http-server)
			echo "make libioplugin-httpd ..."
			${CC} -fPIC -shared -o plugins/libioplugin-httpd.so \
				http-server.c \
				utils/*.c \
				-lpthread -lm ${CFLAGS} -ljpeg -lpng \
				`pkg-config --cflags --libs json-c libsoup-2.4 gio-2.0 glib-2.0 cairo`
			;;
		http-client)
			echo "make libioplugin-httpcient ..."
			${CC} -fPIC -shared -o plugins/libioplugin-httpclient.so \
				http-client.c \
				utils/*.c \
				-lpthread -lm -ljson-c  -Iinclude -ljpeg -lpng \
				`pkg-config --cflags --libs gio-2.0 glib-2.0 libcurl cairo` 
			;;

		*)
			echo "    [WARNING]::unknown target '${TARGET}'"
			exit 1
			;;
	esac
}

if [ "${TARGET}" == "all" ]; then
	for t in "${TARGETS[@]}"
	do
		build $t
	done
else
	build $TARGET
fi
