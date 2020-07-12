GPU ?= 0
LINKER = g++ -std=c++11 

CFLAGS = -g -Wall -Iinclude 
LIBS = -lcairo

CXX_FLAGS = -Wno-sign-compare
CXX_LIBS = -L../caffe/build/lib -lcaffe -lboost_system -lglog -lprotobuf -lgflags

ifeq ($(GPU),0)
CFLAGS += -DCPU_ONLY
else
CFLAGS += -D_GPU
endif

all: resnet-caffe
resnet-caffe: resnet-caffe.cpp
	echo "CXX_LIBS: $(CXX_LIBS)"
	$(LINKER) -o resnet-caffe resnet-caffe.cpp \
		$(CFLAGS) $(LIBS) \
		$(CXX_FLAGS) $(CXX_LIBS)

.PHONY: clean
clean: 
	rm resnet-caffe

