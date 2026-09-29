# TouchVink — firmware for the Synthux Simple Touch (Daisy Seed)
TARGET = TouchVink
# ReverbSc lives in DaisySP-LGPL
USE_DAISYSP_LGPL = 1
# Fits in the 128 kB internal flash. If it grows past that, switch to BOOT_SRAM
# and flash the Daisy bootloader first (see docs/PLAN.md).
APP_TYPE = BOOT_NONE

# Enabling DEBUG = 1 adds serial logging
# DEBUG = 1

LIBDAISY_DIR = lib/libDaisy
DAISYSP_DIR  = lib/DaisySP

# MIDI. The USB port has one owner: MIDI by default (notes, CC and the web
# manual's telemetry), or serial logging with `make NO_USB_MIDI=1`.
# TRS MIDI (D13/D14, modded boards) is opt-in with `make TRS_MIDI=1`; see
# midi/midi_io.h for why it is off by default.
# Run `make clean` when switching: objects don't depend on these flags.
ifndef NO_USB_MIDI
C_DEFS += -DUSB_MIDI
endif
ifdef TRS_MIDI
C_DEFS += -DTRS_MIDI
endif

CPP_SOURCES = TouchVink.cpp dsp/engine.cpp hw/simple_touch.cpp \
              midi/midi_io.cpp midi/telemetry.cpp \
              display/oled_screen.cpp display/oled_ui.cpp display/oled_boot.cpp
C_INCLUDES  = -Icommon -Idsp -Ihw -Imidi -Idisplay

CPP_STANDARD = -std=gnu++17
OPT = -O2

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Control, MIDI and screen code is size-optimized so USB MIDI + the OLED fit
# the 128 kB flash; the DSP (dsp/engine.cpp) keeps OPT above.
$(BUILD_DIR)/TouchVink.o $(BUILD_DIR)/simple_touch.o $(BUILD_DIR)/midi_io.o \
$(BUILD_DIR)/telemetry.o $(BUILD_DIR)/oled_screen.o $(BUILD_DIR)/oled_ui.o \
$(BUILD_DIR)/oled_boot.o: OPT = -Os

# $(MAKE) is quoted: the Daisy Toolchain installs under "C:/Program Files/..."
libs:
	"$(MAKE)" -C $(LIBDAISY_DIR)
	"$(MAKE)" -C $(DAISYSP_DIR)

clean-libs:
	"$(MAKE)" -C $(LIBDAISY_DIR) clean
	"$(MAKE)" -C $(DAISYSP_DIR) clean
