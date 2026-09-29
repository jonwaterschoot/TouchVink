# Desktop build of the DSP engine for listening tests (no hardware needed).
# Run from the repo root:  make -C host && ./host/render
ROOT   := ..
DSP    := $(ROOT)/lib/DaisySP
CXX    ?= g++
CXXFLAGS := -std=gnu++17 -O2 -Wall -DUSE_DAISYSP_LGPL \
  -I$(ROOT)/dsp -I$(ROOT)/common \
  -I$(DSP)/Source -I$(DSP)/Source/Utility -I$(DSP)/DaisySP-LGPL/Source
SRCS := render.cpp $(ROOT)/dsp/engine.cpp \
  $(DSP)/Source/Synthesis/oscillator.cpp \
  $(DSP)/DaisySP-LGPL/Source/Effects/reverbsc.cpp

render: $(SRCS) $(ROOT)/dsp/*.h $(ROOT)/common/config.h
	$(CXX) $(CXXFLAGS) $(SRCS) -o render
	mkdir -p out

clean:
	rm -f render out/*.wav
