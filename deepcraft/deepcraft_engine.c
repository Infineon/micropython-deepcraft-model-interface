/*
 * deepcraft_engine.c — Shared DeepCraft model layer.
 *
 * Should be compiled into both the MicroPython build (alongside deepcraft_interface.c)
 * and any target-side C project that uses the model layer.
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include "deepcraft_interface.h"
#include <stddef.h>

static deepcraft_engine_t *s_model = NULL;

/* receive_dispatch — called by the transport layer when a command/event arrives */
static void receive_dispatch(uint8_t cmd, uint32_t value) {
    if (s_model != NULL) {
        deepcraft_engine_on_receive(s_model, cmd, value);
    }
}

/* deepcraft_engine_init — initializes the engine and registers the receive callback */
void deepcraft_engine_init(deepcraft_engine_t *self,
    model_type_t model_type, interface_type_t *iface) {
    self->model_type = model_type;
    self->interface  = iface;
    self->state      = DEEPCRAFT_STATE_IDLE;
    self->on_event   = NULL;

    s_model = self;

    if (iface->IPC != NULL) {
        iface->IPC->register_receive_cb(iface->IPC, receive_dispatch);
    }
}

/* deepcraft_engine_set_event_cb — sets the user callback for model events */
void deepcraft_engine_set_event_cb(deepcraft_engine_t *self,
    void (*cb)(va_model_events_t event, uint32_t value)) {
    self->on_event = cb;
}

/* deepcraft_engine_start — starts the engine */
void deepcraft_engine_start(deepcraft_engine_t *self) {
    if (self->interface->IPC != NULL) {
        self->interface->IPC->send(self->interface->IPC, DEEPCRAFT_CMD_START, 0U);
    }
    self->state = DEEPCRAFT_STATE_RUNNING;
}

/* deepcraft_engine_stop — stops the engine */
void deepcraft_engine_stop(deepcraft_engine_t *self) {
    if (self->interface->IPC != NULL) {
        self->interface->IPC->send(self->interface->IPC, DEEPCRAFT_CMD_STOP, 0U);
    }
    self->state = DEEPCRAFT_STATE_STOPPING;
}

/* deepcraft_engine_get_state — gets the current state of the engine */
deepcraft_state_t deepcraft_engine_get_state(const deepcraft_engine_t *self) {
    return self->state;
}

/* deepcraft_engine_on_receive — called by the transport layer when a command/event arrives */
void deepcraft_engine_on_receive(deepcraft_engine_t *self,
    uint8_t cmd, uint32_t value) {

    /* Update internal state tracking */
    switch (cmd) {
        case DEEPCRAFT_CMD_VA_READY:
            self->state = DEEPCRAFT_STATE_RUNNING;
            break;
        case DEEPCRAFT_CMD_VA_STOPPED:
            self->state = DEEPCRAFT_STATE_IDLE;
            break;
        case DEEPCRAFT_CMD_VA_ERROR:
            self->state = DEEPCRAFT_STATE_ERROR;
            break;
        default:
            break;
    }

    if (self->on_event == NULL) {
        return;
    }

    /* Set the user callback for model events */
    switch (cmd) {
        case DEEPCRAFT_CMD_VA_READY:
            self->on_event(VA_EVENT_READY, value);
            break;
        case DEEPCRAFT_CMD_VA_WAKEWORD_DETECTED:
            self->on_event(VA_EVENT_WAKEWORD_DETECTED, value);
            break;
        case DEEPCRAFT_CMD_VA_TIMEOUT:
            self->on_event(VA_EVENT_TIMEOUT, value);
            break;
        case DEEPCRAFT_CMD_VA_STOPPED:
            self->on_event(VA_EVENT_STOPPED, value);
            break;
        case DEEPCRAFT_CMD_VA_ERROR:
            self->on_event(VA_EVENT_ERROR, value);
            break;
        default:
            /* Intent: cmd byte IS the intent index */
            self->on_event(VA_EVENT_INTENT, (uint32_t)cmd);
            break;
    }
}
