PROJECT_DIR := ../..
TARGET=$(PROJECT_DIR)/plugins/libaiplugin-caffe.so 

DEBUG ?= 1
OPTIMIZE ?= -O2
USE_GPU ?= 0

CC=gcc -std=gnu99 -D_GNU_SOURCE
LINKER=g++ -std=c++11 -D_GNU_SOURCE

CFLAGS= -Wall -I $(PROJECT_DIR)/include `pkg-config --cflags gio-2.0 glib-2.0`
LIBS = -lm -lpthread -lcairo -ljson-c -ljpeg -lpng `pkg-config --libs gio-2.0 glib-2.0`

CXX_FLAGS=-Wno-sign-compare -I../caffe/include -I../caffe/build/include
CXX_LIBS = -L../caffe/build/lib -lcaffe -lboost_system -lglog -lprotobuf -lgflags

UTILS_SOURCES := $(PROJECT_DIR)/utils/img_proc.c $(PROJECT_DIR)/utils/utils.c
UTILS_OBJECTS := $(UTILS_SOURCES:$(PROJECT_DIR)/utils/%.c=$(PROJECT_DIR)/obj/utils/%.shared.o)

ifeq ($(DEBUG),1)
CFLAGS += -g -D_DEBUG
OPTIMIZE = -O0
endif

ifeq ($(USE_GPU),1)
CFLAGS += -DGPU
else
CFLAGS += -DCPU_ONLY
endif


SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:%.c=%.o)

CXX_SOURCES := $(wildcard *.cpp)
CXX_OBJECTS := $(CXX_SOURCES:%.cpp=%.cpp.obj)

all: $(TARGET)

$(TARGET) : $(CXX_OBJECTS) $(OBJECTS) $(UTILS_OBJECTS)
	$(LINKER) -fPIC -shared -o $@ $^ $(CFLAGS) $(CXX_FLAGS) $(LIBS) $(CXX_LIBS)

$(CXX_OBJECTS): %.cpp.obj : %.cpp
	$(CXX) -fPIC -o $@ -c $< $(CFLAGS) $(CXX_FLAGS)

$(OBJECTS): %.o : %.c
	$(CC) -fPIC -o $@ -c $< $(CFLAGS)

$(UTILS_OBJECTS): $(PROJECT_DIR)/obj/utils/%.shared.o : $(PROJECT_DIR)/utils/%.c
	$(CC) -fPIC -o $@ -c $< $(CFLAGS)

.PHONY: clean
clean:
	rm -f *.cpp.obj *.o $(TARGET)


