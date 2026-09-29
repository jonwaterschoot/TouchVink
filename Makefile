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

CPP_SOURCES = TouchVink.cpp dsp/engine.cpp hw/simple_touch.cpp
C_INCLUDES  = -Icommon -Idsp -Ihw

CPP_STANDARD = -std=gnu++17
OPT = -O2

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

libs:
	cd $(LIBDAISY_DIR) && $(MAKE)
	cd $(DAISYSP_DIR) && $(MAKE)

clean-libs:
	cd $(LIBDAISY_DIR) && $(MAKE) clean
	cd $(DAISYSP_DIR) && $(MAKE) clean
