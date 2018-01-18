SRC       = omx.cpp \
	    cOmxCore.cpp \
	    cOmxClock.cpp \
	    cOmxReader.cpp \
	    cOmxVideo.cpp \
	    cOmxAudio.cpp \
	    cSwAudio.cpp \
	    cOmxVideoPlayer.cpp \
	    cOmxAudioPlayer.cpp \
	    cPcmMap.cpp \
	    ../shared/utils/cLog.cpp \
	    ../shared/utils/cKeyboard.cpp \
	    ../shared/nanoVg/cRaspWindow.cpp \
	    ../shared/nanoVg/cVg.cpp \
	    ../shared/dvb/cDvb.cpp \

#           -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -mfloat-abi=hard -mfpu=vfp -march=armv6zk
#           -mstructure-size-boundary=32 \

CFLAGS   += -std=c++0x \
	    -g -O2 \
	    -fPIC -ftree-vectorize -fomit-frame-pointer \
	    -Wall -Wno-psabi -Wno-deprecated-declarations \
	    -mabi=aapcs-linux -mno-apcs-stack-check -mno-sched-prolog \
	    -D PIC -D _REENTRANT \
	    -D _LARGEFILE64_SOURCE -D _FILE_OFFSET_BITS=64 -D OMX_SKIP64BIT \
	    -U _FORTIFY_SOURCE \
	    -mcpu=cortex-a53 -mtune=cortex-a53 -mfloat-abi=hard -mfpu=neon-fp-armv8 -mneon-for-64bits

INCLUDES  = -I$(SDKSTAGE)/opt/vc/include \
	    -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host \
	    -I$(SDKSTAGE)/opt/vc/include/interface/vcos/pthreads \
	    -I$(SDKSTAGE)/opt/vc/include/interface/vmcs_host/linux \
	    -I$(SDKSTAGE)/usr/local/include/ \

LDFLAGS  += -L ./ \
	    -L $(SDKSTAGE)/opt/vc/lib/ \
	    -l pthread \
	    -l brcmGLESv2 -l brcmEGL -l bcm_host \
	    -l vcos -l vchostif -l vchiq_arm -l openmaxil \
	    -l avutil -l avcodec -l avformat -l swscale -l swresample \
	    -l asound \

OBJS    += $(filter %.o,$(SRC:.cpp=.o))

all: omx

%.o: %.cpp
	@rm -f $@
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@

version:
	bash gen_version.sh > version.h

omx:    version $(OBJS)
	$(CXX) $(LDFLAGS) -o omx $(OBJS)

clean:
	rm -f *.o
	rm -f *.log
	rm -f omx

.PHONY: clean rebuild

rebuild:
	make clean && make

ifndef LOGNAME
SDKSTAGE = /SysGCC/Raspberry/arm-linux-gnueabihf/sysroot
endif

CC      := arm-linux-gnueabihf-gcc
CXX     := arm-linux-gnueabihf-g++
