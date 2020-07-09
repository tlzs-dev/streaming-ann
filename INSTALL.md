Streaming ANN: A deep-learning framework implemented in C

License
-------
[The MIT License (MIT)](https://opensource.org/licenses/MIT)


Dependencies
-------------

These dependencies are required:

    ----------------------------------------
    Library         |  Description
    ----------------|-----------------------
    gstreamer-1.0   | video processing (camera: rtsp/motion-jpeg/v4l2/ksvideo; video file: mp4/avi/mkv ...)
    libdarknet      | an open source neural network framework written in C and CUDA. 
    libjpeg         | image format conversion
    libpng          | image format conversion
    libcairo        | image processing / renderering
    ----------------------------------------

Optional dependencies: 

    ----------------------------------------
    Library         |  Description
    ----------------|-----------------------
    gtk+-3.0        | GUI
    libsoup-2.4     | JSON-RPC HTTP server
    libcurl         | http client
    json-c          | json 
    ----------------------------------------

## Build

### install dependencies

#### Linux / Debian 

    $ sudo apt-get install build-essential libgstreamer1.0-dev gstreamer1.0-libav \
        gstreamer1.0-plugins-{base,good,bad,ugly} \
        libjpeg62-turbo-dev libcairo2-dev libpng-dev
    
    $ cd /tmp && git clone https://github.com/pjreddie/darknet.git
    $ cd darknet
    ( if NVIDIA GPU hardware is present, check the GPU's architecture and modify 'Makefile' to add GPU support)
        $ export GPU_ARCH=61
        $ sed -i.bak -e 's/GPU=0/GPU=1/g' -e "s/# ARCH=.*/ARCH=-gencode arch=compute_${GPU_ARCH},code=compute_${GPU_ARCH}/g" Makefile
        
        (the GPU's architecture can be obtained through the 'cuda-tool' app under the 'tools' folder.)
        ( $ cd tools && make && export GPU_ARCH=`./cuda-tool --get-arch` )
    $ make
    
    (optional)
    $ sudo apt-get install libgtk-3-dev libjson-c-dev libsoup2.4-dev libcurl4-gnutls-dev
    
#### Windows

    step 0. install MSYS2(x86_64) (https://www.msys2.org/)
    
    step 1. open an msys(64bits) terminal and install dependencies
    $ pacman -Syu   # update package database
    $ pacman -Sy base-devel git \
        mingw64/mingw-w64-x86_64-gstreamer \
        mingw64/mingw-w64-x86_64-gst-libav \
        mingw64/mingw-w64-x86_64-gst-plugins-bad \
        mingw64/mingw-w64-x86_64-gst-plugins-base \
        mingw64/mingw-w64-x86_64-gst-plugins-good \
        mingw64/mingw-w64-x86_64-gst-plugins-ugly \
    $ pacman -Sy mingw64/mingw-w64-x86_64-cairo mingw64/mingw-w64-x86_64-libjpeg-turbo mingw64/mingw-w64-x86_64-libpng
    
    $ pacman -Sy mingw64/mingw-w64-x86_64-gtk3 \
        mingw64/mingw-w64-x86_64-libsoup \
        mingw64/mingw-w64-x86_64-curl \
        mingw64/mingw-w64-x86_64-json-c
        
### build

    $ git clone https://github.com/chehw/streaming-ann.git
    $ cd streaming-ann && mkdir -p plugins && make clean all
    
    (optional: build demo apps)
    $ cd demo 
    $ ln -s ../plugins ./ 
    $ ln -s ../lib ./
    $ ln -s ../include ./
    $ ./make.sh demo-01
    $ ./make.sh demo-(nn)
    
