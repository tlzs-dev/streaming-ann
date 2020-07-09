all: cuda-tool

cuda-tool: cuda-tool.c cuda-api.c
	gcc -std=gnu99 -Wall -D_GNU_SOURCE -o cuda-tool cuda-tool.c cuda-api.c -I /usr/local/cuda/include -L /usr/local/cuda/lib64 -lcuda  -I../include -lm -lpthread

.PHONY: clean
clean:
	rm cuda-tool
