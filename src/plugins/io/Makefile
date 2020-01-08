TARGET=test1

TARGETS  = lib/libioproxy-httpd.so.1 
TARGETS += lib/libioproxy-tcpd.so.1
TARGETS += lib/libioproxy-httpclient.so.1
TARGETS += lib/libioproxy-default.so.1




CC=gcc -std=gnu99 -D_GNU_SOURCE
LINKER=gcc -std=gnu99 -D_GNU_SOURCE

CFLAGS = -Iinclude -g -Wall -D_DEBUG
CFLAGS += $(shell pkg-config --cflags libsoup-2.4 gstreamer-1.0)

LIBS = -lm -lpthread -ljson-c -ljpeg -lpng -lcairo

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:%.c=obj/%.o) 
OBJECTS := $(filter-out obj/test1.o,$(OBJECTS))

UTILS_SRCS := $(wildcard utils/*.c)
UTILS_OBJS := $(UTILS_SRCS:utils/%.c=obj/utils/%.o)

all: $(TARGETS) test1 tests/test-inputs


$(OBJECTS): obj/%.o : %.c
	$(CC) -fPIC -o $@ -c $< $(CFLAGS)

obj/input-souce.o: io-module/input-source.c
	$(CC) -fPIC -o $@ -c $< $(CFLAGS)

lib/libioproxy-httpd.so.1: $(OBJECTS) $(UTILS_SRCS)
	$(LINKER) -fPIC -shared -o $@ obj/http-server.o obj/auto-buffer.o obj/io-input.o \
		utils/input-frame.c utils/img_proc.c utils/utils.c \
		$(CFLAGS) \
	    $(LIBS)  `pkg-config --libs libsoup-2.4`

lib/libioproxy-tcpd.so.1: $(OBJECTS)
	$(LINKER) -fPIC -shared -o $@ obj/tcp-server.o obj/auto-buffer.o obj/io-input.o -lm -lpthread -ljson-c

lib/libioproxy-httpclient.so.1: $(OBJECTS)
	$(LINKER) -fPIC -shared -o $@ obj/http-client.o obj/auto-buffer.o obj/io-input.o \
		utils/input-frame.c utils/img_proc.c utils/utils.c \
		$(CFLAGS) \
		-lm -lpthread -ljson-c -lcurl

lib/libioproxy-default.so.1: $(OBJECTS) obj/input-souce.o
	$(LINKER) -fPIC -shared -o $@ obj/default-plugin.o obj/auto-buffer.o obj/io-input.o \
		utils/input-frame.c utils/img_proc.c utils/utils.c  \
		obj/input-souce.o \
		$(CFLAGS) \
		-lm -lpthread -ljson-c -lpng -ljpeg -lcairo \
		`pkg-config --cflags --libs gstreamer-1.0` 

$(UTILS_OBJS): obj/utils/%.o : utils/%.c
	$(CC) -o $@ -c $< $(CFLAGS)


test1: $(TARGETS) test1.c $(UTILS_OBJS)
	gcc -g -Wall -D_DEBUG -D_GNU_SOURCE -o test1 \
	    test1.c io-input.c plugins-helpler.c \
	    $(UTILS_OBJS) $(LIBS) -ldl -Iinclude \
		$(shell pkg-config --cflags --libs gtk+-3.0 gstreamer-1.0)

tests/test-inputs: obj/input-souce.o tests/test-inputs.c
	cd tests && ./make.sh

.PHONY: clean
clean:
	rm -f obj/*.o obj/utils/*.o  $(TARGETS) tests/test-inputs
