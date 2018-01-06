SRC=       cPcmRemap.cpp \
	   cLog.cpp \
	   cKeyboard.cpp \
	   cOmxThread.cpp \
	   cOmxCoreComponent.cpp \
	   cOmxCoreTunnel.cpp \
	   cOmxClock.cpp \
	   cOmxReader.cpp \
	   cOmxPlayerVideo.cpp \
	   cOmxVideo.cpp \
	   cOmxPlayerAudio.cpp \
	   cOmxAudio.cpp \
	   cSwAudio.cpp \
	   omxAlsa.cpp \
	   omx.cpp

CFLAGS=    -std=c++0x -O3 -fPIC -ftree-vectorize -fomit-frame-pointer \
	   -Wall -Wno-psabi -Wno-deprecated-declarations \
	   -mfloat-abi=hard -mfpu=vfp -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk \
	   -mstructure-size-boundary=32 -mabi=aapcs-linux -mno-apcs-stack-check -mno-sched-prolog \
	   -D PIC  -D _REENTRANT \
	   -D __STDC_CONSTANT_MACROS  -D __STDC_LIMIT_MACROS \
	   -D_LARGEFILE64_SOURCE  -D_FILE_OFFSET_BITS=64 \
	   -D TARGET_POSIX -D TARGET_LINUX  -D TARGET_RASPBERRY_PI  -D __VIDEOCORE4__ \
	   -D OMX  -D HAVE_OMXLIB  -D OMX_SKIP64BIT \
	   -D USE_EXTERNAL_OMX \
	   -D USE_EXTERNAL_LIBBCM_HOST \
	   -D USE_EXTERNAL_FFMPEG \
	   -D HAVE_LIBAVUTIL_AVUTIL_H \
	   -D HAVE_LIBAVFORMAT_AVFORMAT_H \
	   -D HAVE_LIBAVFILTER_AVFILTER_H \
	   -D HAVE_LIBAVCODEC_AVCODEC_H \
	   -D HAVE_LIBSWRESAMPLE_SWRESAMPLE_H \
	   -D HAVE_LIBAVUTIL_OPT_H \
	   -D HAVE_LIBAVUTIL_MEM_H \
	   -U _FORTIFY_SOURCE \

INCLUDES = -I$(SDKSTAGE)/usr/local/include/ \
	   -I$(SDKSTAGE)/opt/vc/include \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host/linux \

LDFLAGS+= -L ./ \
	  -L $(SDKSTAGE)/opt/vc/lib/ \
	  -l pthread \
	  -l asound \
	  -l brcmGLESv2 \
	  -l brcmEGL \
	  -l bcm_host \
	  -l WFC \
	  -l EGL \
	  -l GLESv2 \
	  -l vcos \
	  -l vchostif \
	  -l vchiq_arm \
	  -l openmaxil \
	  -l avutil \
	  -l avcodec \
	  -l avformat \
	  -l swscale \
	  -l swresample \

OBJS    += $(filter %.o,$(SRC:.cpp=.o))

all:    omx

%.o: %.cpp
	@rm -f $@
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@ -Wno-deprecated-declarations

version:
	bash gen_version.sh > version.h

omx:    version $(OBJS)
	$(CXX) $(LDFLAGS) -o omx $(OBJS)

clean:
	@rm -f *.o
	@rm -f omx
	@rm -f omx.old.log omx.log

.PHONY: clean rebuild

rebuild:
	make clean && make

ifndef LOGNAME
SDKSTAGE  = /SysGCC/Raspberry/arm-linux-gnueabihf/sysroot
endif
CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++
