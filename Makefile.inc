ifndef LIBAUDIO_INC
LIBAUDIO_INC:=y

MAKEFILES_ROOT?=$(LIBAUDIO_ROOT)submodules/makefiles/
LIBCOMMON_ROOT?=$(LIBAUDIO_ROOT)submodules/common/
include $(LIBCOMMON_ROOT)Makefile.inc

LIBAUDIO_CXXFLAGS += -fno-rtti
OPENCORE_AUDIO_CXXFLAGS += -fno-rtti

ifneq ($(PLATFORM), windows)
OPENCORE_AUDIO_CXXFLAGS += -Wno-narrowing
endif

ifeq ($(PLATFORM), openbsd)
LDFLAGS += -lm
endif

ifeq ($(PLATFORM), windows)
ifndef WIN64
USE_OPENCORE_MP3:=y
USE_OPENCORE_AAC:=y
endif
endif

# Observed some problematic MP3 files that extaudiofile won't decode.
ifeq ($(PLATFORM),darwin)
USE_OPENCORE_MP3:=y
endif

ifeq (, $(filter $(PLATFORM),windows darwin))
USE_OPENCORE_MP3:=y
USE_OPENCORE_AAC:=y
endif
USE_OPENCORE_AMR:=y

include $(LIBAUDIO_ROOT)third_party/Makefile.inc

LIBAUDIO_CFLAGS += -I$(LIBKISSFFT_ROOT)

LIBAUDIO_SRC=\
   $(LIBAUDIO_ROOT)src/codecs/seek.cc \
   $(LIBAUDIO_ROOT)src/codecs/wav.cc \
   $(LIBAUDIO_ROOT)src/codecs/ogg.cc \
   $(LIBAUDIO_ROOT)src/codecs/vorbisfile.cc \
   $(LIBAUDIO_ROOT)src/codecs/opusfile.cc \
   $(LIBAUDIO_ROOT)src/codecs/flac.cc \
   $(LIBAUDIO_ROOT)src/codec.cc \
   $(LIBAUDIO_ROOT)src/wakelock.cc \
   $(LIBAUDIO_ROOT)src/dev/wrapper.cc \
   $(LIBAUDIO_ROOT)src/player.cc

ifneq (, $(filter $(PLATFORM),netbsd sunos))
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/dev/devaudio.cc
LIBAUDIO_CFLAGS+=-DUSE_DEVAUDIO
endif

ifneq (, $(filter $(PLATFORM),freebsd linux))
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/dev/oss.cc
LIBAUDIO_CFLAGS+=-DUSE_OSS
endif

ifeq ($(PLATFORM), linux)
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/dev/alsa.cc
LIBAUDIO_CFLAGS+=-DUSE_ALSA
LDFLAGS+=-lasound
endif

ifeq ($(PLATFORM), openbsd)
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/dev/sndio.cc
LIBAUDIO_CFLAGS+=-DUSE_SNDIO
LDFLAGS+=-lsndio
endif

ifeq ($(PLATFORM), windows)
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/mediafoundation.cc \
   $(LIBAUDIO_ROOT)src/dev/wasapi.cc \
   $(LIBAUDIO_ROOT)src/dev/winmm.cc
LIBAUDIO_CFLAGS+=-DUSE_WASAPI -DUSE_WINMM

ifndef WIN64
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/winimport.cc \
   $(LIBAUDIO_ROOT)imports/mf-x86.asm

$(LIBAUDIO_ROOT)imports/mf-x86.o: $(LIBAUDIO_ROOT)imports/mf-x86
	perl $(LIBAUDIO_ROOT)scripts/imports.pl < $< > $@.asm
	nasm -f win32 $@.asm -o $@
endif

endif

ifeq ($(PLATFORM), darwin)
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/coreaudio.cc \
   $(LIBAUDIO_ROOT)src/dev/coreaudio.cc
LIBAUDIO_CFLAGS+=-DUSE_COREAUDIO
LDFLAGS+=-framework AudioToolbox
LDFLAGS+=-framework IOKit
endif

ifneq (, $(USE_OPENCORE_MP3)$(USE_OPENCORE_AAC))
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/adts.cc
endif
ifneq (, $(USE_OPENCORE_AMR)$(USE_OPENCORE_MP3)$(USE_OPENCORE_AAC))
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/mp4.cc
LIBAUDIO_CFLAGS+=-DHAVE_MP4_DEMUX
endif

ifdef USE_OPENCORE_MP3
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/opencore-mp3.cc
LIBAUDIO_CFLAGS+= -DUSE_OPENCORE_MP3
endif

ifdef USE_OPENCORE_AAC
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)src/codecs/opencore-aac.cc
LIBAUDIO_CFLAGS+= -DUSE_OPENCORE_AAC
endif

ifdef USE_OPENCORE_AMR
LIBAUDIO_SRC+=\
   $(LIBAUDIO_ROOT)third_party/amrwb-wrapper.cpp \
   $(LIBAUDIO_ROOT)src/codecs/opencore-amr.cc
LIBAUDIO_CFLAGS+= -DUSE_OPENCORE_AMR
endif

LIBAUDIO_SUBLIBS+=LIBAUDIO_SRC

LIBAUDIO_OBJS := $(foreach i, $(LIBAUDIO_SUBLIBS), $(shell $(SRC2OBJ) $($(i))))

LIBAUDIO:=$(LIBAUDIO_ROOT)$(LIBPREFIX)audio$(LIBSUFFIX)

define \n


endef

$(LIBAUDIO): ${LIBAUDIO_OBJS}
ifdef WINDOWS
	rm -f $(LIBAUDIO_ROOT)thirdparty_*.lib
	$(foreach i, $(LIBAUDIO_SUBLIBS), $(LIBWRAPPER) $(shell $(SRC2OBJ) $($(i)))${\n}mv $@ $(LIBAUDIO_ROOT)thirdparty_$(i).lib${\n})
	$(LIB) /out:$@ $(LIBAUDIO_ROOT)thirdparty_*.lib
else
	$(LIBWRAPPER) $(LIBAUDIO_OBJS)
endif

LIBAUDIO_CFLAGS += -I${LIBCOMMON_ROOT}include
LIBAUDIO_CFLAGS += -I${LIBLIBAUDIO_ROOT}include
LIBAUDIO_CXXFLAGS += ${LIBAUDIO_CFLAGS}

-include $(LIBAUDIO_ROOT)depend.mk
-include $(LIBAUDIO_ROOT)depend.$(PLATFORM).mk

endif