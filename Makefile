SRC=       cOmxCoreComponent.cpp \
	   cOmxCoreTunnel.cpp \
	   cOmxClock.cpp \
	   cOmxReader.cpp \
	   cOmxPlayerVideo.cpp \
	   cOmxVideo.cpp \
	   cOmxPlayerAudio.cpp \
	   cOmxAudio.cpp \
	   cSwAudio.cpp \
	   cPcmRemap.cpp \
	   ../shared/utils/cLog.cpp \
	   ../shared/utils/cKeyboard.cpp \
	   ../shared/nanoVg/cRaspWindow.cpp \
	   ../shared/nanoVg/cVg.cpp \
	   omx.cpp \

INCLUDES = -I$(SDKSTAGE)/usr/local/include/ \
	   -I$(SDKSTAGE)/opt/vc/include \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads \
	   -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host/linux \

CFLAGS=    -std=c++0x \
	   -O3 \
	   -fPIC \
	   -ftree-vectorize \
	   -fomit-frame-pointer \
	   -g \
	   -Wall \
	   -Wno-psabi \
	   -Wno-deprecated-declarations \
	   -mfloat-abi=hard \
	   -mfpu=vfp \
	   -mcpu=arm1176jzf-s \
	   -mtune=arm1176jzf-s \
	   -march=armv6zk \
	   -mstructure-size-boundary=32 \
	   -mabi=aapcs-linux \
	   -mno-apcs-stack-check \
	   -mno-sched-prolog \
	   -D PIC \
	   -D _REENTRANT \
	   -D _LARGEFILE64_SOURCE \
	   -D _FILE_OFFSET_BITS=64 \
	   -D OMX_SKIP64BIT \
	   -U _FORTIFY_SOURCE \

LDFLAGS+=  -L ./ \
	   -L $(SDKSTAGE)/opt/vc/lib/ \
	   -l pthread \
	   -l asound \
	   -l brcmGLESv2 \
	   -l brcmEGL \
	   -l bcm_host \
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
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@

omx:    $(OBJS)
	$(CXX) $(LDFLAGS) -o omx $(OBJS)

clean:
	@rm -f *.o
	@rm -f *.log
	@rm -f omx

.PHONY: clean rebuild

rebuild:
	make clean && make

ifndef LOGNAME
SDKSTAGE = /SysGCC/Raspberry/arm-linux-gnueabihf/sysroot
endif

CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++
