.PHONY: all all-phony clean depend
all: all-phony

CFLAGS += -O2 -Wall
WINDOWS_SUBSYSTEM=console
include Makefile.inc
CXXFLAGS += $(CFLAGS)

all-phony: $(LIBCOMMON) $(LIBAUDIO) test$(EXESUFFIX)

TESTDEPENDS := $(LIBCOMMON) $(LIBAUDIO) $(XP_SUPPORT_OBJS)
TESTFLAGS := $(CXXFLAGS) $(LIBAUDIO_CXXFLAGS)
TESTLIBS := -L. -L$(LIBCOMMON_ROOT) -laudio -lcommon $(LDFLAGS) $(CXXLIBS)

test$(EXESUFFIX): src/test.cc $(TESTDEPENDS)
	$(CXX) -o $@ $(TESTFLAGS) $< $(TESTLIBS)

clean:
	rm -f $(LIBCOMMON) $(LIBCOMMON_OBJS)
	rm -f $(LIBAUDIO) $(LIBAUDIO_OBJS)
	rm -f test$(EXESUFFIX)
	rm -f test.obj

export
depend:
	env PROJECT=LIBAUDIO $(DEPEND) src/*.cc src/codecs/*.cc src/dev/*.cc \
	 > depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=ALAC $(DEPEND) $(ALAC_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) $(OPENCORE_MP3_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) $(OPENCORE_AAC_SRC_A) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) $(OPENCORE_AAC_SRC_B) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) $(OPENCORE_AMR_SRC_A) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) $(OPENCORE_AMR_SRC_B) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=OPENCORE_AUDIO $(DEPEND) third_party/amrwb-wrapper.cpp \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBOGG $(DEPEND) $(LIBOGG_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBOPUS $(DEPEND) $(LIBOPUS_BASE_SRC) $(LIBOPUS_FLOAT_SRC) $(LIBOPUS_FIXED_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBOPUSFILE $(DEPEND) $(LIBOPUSFILE_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBVORBIS $(DEPEND) $(LIBVORBIS_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBFLAC $(DEPEND) $(LIBFLAC_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBKISSFFT $(DEPEND) $(LIBKISSFFT_SRC) \
	 >> depend.mk.tmp
	env ROOT=LIBAUDIO PROJECT=LIBRESAMPLER $(DEPEND) $(LIBRESAMPLER_SRC) \
	 >> depend.mk.tmp
	mv depend.mk.tmp depend.mk

