TESTS=tests/test-io-inputs tests/test-plugins tests/test-ai-engines
DEBUG ?= 1
PLUGINS_PATH=$(PWD)/plugins

CC=gcc -std=gnu99 -D_GNU_SOURCE
LINKER=gcc -std=gnu99 -D_GNU_SOURCE
AR=ar crf

CFLAGS = -g -Wall -Iinclude -D_DEBUG

ifeq ($(DEBUG),1)
CFLAGS += -D_DEBUG
endif

LDFLAGS = $(CFLAGS)

SOURCES := $(wildcard src/*.c)
OBJECTS := $(SOURCES:src/%.c=obj/%.o)

UTILS_CFLAGS = $(CFLAGS) `pkg-config --cflags gio-2.0 glib-2.0`
UTILS_LDFLAGS = $(UTILS_CFLAGS)
UTILS_LIBS = -lm -lpthread -ljson-c -ljpeg -lpng -lcairo -ldl `pkg-config --libs gio-2.0 glib-2.0` 

UTILS_SOURCES := $(wildcard utils/*.c)
UTILS_OBJECTS := $(UTILS_SOURCES:utils/%.c=obj/utils/%.o)


all: do_init lib/libann-utils.a lib/libann-utils.so plugins tests/test-plugins tests

lib/libann-utils.a: $(UTILS_OBJECTS) $(OBJECTS)
	echo "[libann-utils.a]:: " $^
	$(AR) $@ $^

lib/libann-utils.so: $(UTILS_SOURCES) $(SOURCES)
	$(LINKER) -fPIC -shared $(UTILS_LDFLAGS) -o $@ $(UTILS_SOURCES) $(SOURCES)  $(UTILS_LIBS)

plugins: $(OBJECTS)
	cd src/plugins/io && ./make.sh all
	cd src/plugins/ai-engines && ./make.sh

$(UTILS_OBJECTS): obj/utils/%.o : utils/%.c
	$(CC) -o $@ -c $< $(UTILS_CFLAGS)

$(OBJECTS) : obj/%.o : src/%.c
	$(CC) -o $@ -c $< $(CFLAGS)


tests/test-plugins: tests/test-plugins.c lib/libann-utils.a
	gcc -g -Wall $(LDFLAGS) -o $@ $^ $(UTILS_LDFLAGS) $(UTILS_LIBS)

tests/test-io-inputs: tests/test-io-inputs.c lib/libann-utils.a
	gcc -g -Wall $(LDFLAGS) -o $@ $^ $(UTILS_LDFLAGS) $(UTILS_LIBS) \
		`pkg-config --cflags --libs gstreamer-1.0`


tests/test-ai-engines: tests/test-ai-engines.c lib/libann-utils.a
	gcc -g -Wall $(LDFLAGS) -o $@ $^ $(UTILS_LDFLAGS) $(UTILS_LIBS) 
		

.PHONY: do_init clean tests
do_init:
	mkdir -p plugins obj obj/utils lib

clean:
	rm -f obj/*.o obj/utils/*.o lib/libann-utils.*

tests: $(TESTS)
