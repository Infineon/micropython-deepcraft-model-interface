# deepcraft_interface.mk — Build integration fragment
#
# Common DeepCraft model interface layer for both CM33 and CM55.
#

DEEPCRAFT_MODEL_LIB_DIR ?= $(dir $(lastword $(MAKEFILE_LIST)))

# ── CM33 / MicroPython port (TOP is set by py/mkenv.mk) ──────────────────────
ifneq ($(TOP),)

INC   += -I$(DEEPCRAFT_MODEL_LIB_DIR)deepcraft
SRC_C += $(DEEPCRAFT_MODEL_LIB_DIR)deepcraft/deepcraft_engine.c
SRC_C += $(DEEPCRAFT_MODEL_LIB_DIR)deepcraft/deepcraft_interface.c

# ── CM55 / ModusToolbox (CORE_NAME == CM55) ───────────────────────────────────
# Only the shared header (deepcraft_interface.h) is needed from this library.
# The IPC implementation and model API live entirely in the CM55 project itself
# (proj_cm55/deepcraft_ipc_cm55.h/.c).
else ifeq ($(CORE_NAME),CM55)

INCLUDES += $(DEEPCRAFT_MODEL_LIB_DIR)deepcraft

endif
