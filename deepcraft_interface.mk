# deepcraft_interface.mk — Build integration fragment
#
# Common DeepCraft model interface layer for both CM33 and CM55.
#
# CM33 / MicroPython port:
#   DEEPCRAFT_MODEL_LIB_DIR := ../../lib/micropython-deepcraft-model-interface/
#   include $(DEEPCRAFT_MODEL_LIB_DIR)deepcraft_interface.mk
#
# CM55 / ModusToolbox project:
#   DEEPCRAFT_MODEL_LIB_DIR := $(SEARCH_micropython-deepcraft-model-interface)/
#   include $(DEEPCRAFT_MODEL_LIB_DIR)deepcraft_interface.mk
#
# Layout:
#   src/deepcraft_interface.h  — shared types: model_type_t, interface_type_t,
#                                 va_model_events_t, vtable, API declarations
#                                 (included by BOTH cores)
#   src/deepcraft_engine.c     — shared pure-C model logic (both cores)
#   src/deepcraft_interface.c  — CM33 MicroPython bridge (machine.IPC adapter)
#   cm55/mtb-example-psoc-edge-voice-assistant-deploy-mpy/
#       proj_cm55/deepcraft_ipc_cm55.h/.c — CM55 IPC implementation
#                                           (owned by the CM55 project)
#       shared/include/ipc_communication.h
#       shared/source/COMPONENT_CM55/cm55_ipc_communication.c
# ─────────────────────────────────────────────────────────────────────────────

DEEPCRAFT_MODEL_LIB_DIR ?= $(dir $(lastword $(MAKEFILE_LIST)))

# ── CM33 / MicroPython port (TOP is set by py/mkenv.mk) ──────────────────────
ifneq ($(TOP),)

INC   += -I$(DEEPCRAFT_MODEL_LIB_DIR)src
SRC_C += $(DEEPCRAFT_MODEL_LIB_DIR)src/deepcraft_engine.c
SRC_C += $(DEEPCRAFT_MODEL_LIB_DIR)src/deepcraft_interface.c

# ── CM55 / ModusToolbox (CORE_NAME == CM55) ───────────────────────────────────
# Only the shared header (deepcraft_interface.h) is needed from this library.
# The IPC implementation and model API live entirely in the CM55 project itself
# (proj_cm55/deepcraft_ipc_cm55.h/.c).
else ifeq ($(CORE_NAME),CM55)

INCLUDES += $(DEEPCRAFT_MODEL_LIB_DIR)src

endif
