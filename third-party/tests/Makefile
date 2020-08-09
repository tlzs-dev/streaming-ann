GPU ?= 0
LINKER = g++ -std=c++11 
PROJECT_DIR := ../..

CFLAGS = -g -Wall -Iinclude -I$(PROJECT_DIR)/include
LIBS = -lcairo -ljson-c

CXX_FLAGS = -Wno-sign-compare
CXX_LIBS = -L../caffe/build/lib -lcaffe -lboost_system -lglog -lprotobuf -lgflags




ifeq ($(GPU),0)
CFLAGS += -DCPU_ONLY
else
CFLAGS += -D_GPU
endif

all: resnet-caffe part_action-caffe
resnet-caffe: resnet-caffe.cpp
	echo "CXX_LIBS: $(CXX_LIBS)"
	$(LINKER) -o $@ $^ \
		$(CFLAGS) $(LIBS) \
		$(CXX_FLAGS) $(CXX_LIBS)


part_action-caffe: part_action-caffe.cpp $(PROJECT_DIR)/obj/utils/img_proc.o
	echo "CXX_LIBS: $(CXX_LIBS)"
	$(LINKER) -o $@ $^ \
		$(CFLAGS) $(LIBS) -ljpeg -lpng `pkg-config --cflags --libs glib-2.0 gio-2.0` \
		$(CXX_FLAGS) $(CXX_LIBS)

$(PROJECT_DIR)/obj/utils/img_proc.o: $(PROJECT_DIR)/utils/img_proc.c
	gcc -std=gnu99 -o $@ -c $< $(CFLAGS) `pkg-config --cflags glib-2.0 gio-2.0` 


test_caffe_plugin: test_caffe_plugin.c
	gcc -std=gnu99 -g -Wall -I$(PROJECT_DIR)/include  \
		-o $@ $< \
		$(PROJECT_DIR)/lib/libann-utils.a  \
		-lm -lpthread -ljson-c -ldl \
		-lcairo -ljpeg \
		`pkg-config --cflags --libs gio-2.0 glib-2.0`
		
		
.PHONY: clean
clean: 
	rm resnet-caffe

