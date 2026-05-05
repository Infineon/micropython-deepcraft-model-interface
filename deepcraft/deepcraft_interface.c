/*
 * deepcraft_interface.c — MicroPython C module: "deepcraft_model"
 *
 * Bridges a MicroPython transport object into the shared deepcraft_engine_t C API
 * (deepcraft_engine.c).  No transport-specific code lives here; the caller
 * constructs their own transport object (e.g. machine.IPC)
 * and passes it to the constructor.
 *
 * Interface specs (MicroPython object passed as first constructor argument):
 *   interface.init()                                          (called by constructor)
 *   interface.send(cmd, value, target_client_id)
 *   interface.register_client(recv_client_id, callback, ep_id, ep_addr)
 *   interface.enable_core(target_id)
 *
 * Copyright (c) 2026 Infineon Technologies AG
 * SPDX-License-Identifier: MIT
 */

#include "py/runtime.h"
#include "py/obj.h"
#include "deepcraft_interface.h"

/* ── Default transport client/endpoint IDs ──────────────────────────────── */
/*
 * Defaults chosen for the IPC-pipe transport (PSoC Edge).
 * Override at build time via CFLAGS to match your transport/project.
 */
#ifndef DEEPCRAFT_DEFAULT_RECV_CLIENT_ID
#define DEEPCRAFT_DEFAULT_RECV_CLIENT_ID   (3U)
#endif
#ifndef DEEPCRAFT_DEFAULT_RECV_EP_ADDR
#define DEEPCRAFT_DEFAULT_RECV_EP_ADDR     (1U)
#endif
#ifndef DEEPCRAFT_DEFAULT_TARGET_CLIENT_ID
#define DEEPCRAFT_DEFAULT_TARGET_CLIENT_ID (5U)
#endif
#ifndef DEEPCRAFT_DEFAULT_TARGET_ID
#define DEEPCRAFT_DEFAULT_TARGET_ID        (1U)
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * MicroPython transport adapter
 *
 * Wraps a MicroPython transport object (machine.IPC)
 * in a deepcraft_interface_t vtable so the shared deepcraft_engine.c logic
 * can call it without knowing anything about MicroPython.
 *
 * base MUST be the first field so that &adapter can be safely cast to
 * deepcraft_interface_t *.
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    deepcraft_interface_t   base;             /* vtable — MUST be first          */
    mp_obj_t                py_transport;     /* MicroPython transport object         */
    mp_int_t                target_client_id; /* passed to transport.send()      */
    mp_int_t                recv_client_id;   /* passed to register_client()     */
    mp_int_t                recv_ep_addr;     /* passed to register_client()     */
} mpy_adapter_t;

/*
 * Storage for the C receive callback installed by deepcraft_engine_init().
 * mpy_adapter_receive_handler (registered with the transport object) reads from here.
 */
static void (*s_c_receive_cb)(uint8_t cmd, uint32_t value) = NULL;

/*
 * mpy_adapter_receive_handler — MicroPython-callable C function registered with the transport.
 *
 * The transport calls registered callables as: fn(cmd, value, client_id).
 * This shim forwards cmd and value to the deepcraft_engine.c dispatcher.
 */
static mp_obj_t mpy_adapter_receive_handler(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    uint8_t  cmd   = (uint8_t)mp_obj_get_int(args[0]);
    uint32_t value = (uint32_t)mp_obj_get_int(args[1]);
    /* args[2] = client_id, consumed by the transport layer — ignored here */
    if (s_c_receive_cb != NULL) {
        s_c_receive_cb(cmd, value);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mpy_receive_shim_obj, 3, 3,
    mpy_adapter_receive_handler);

/* vtable.send — calls transport.send(cmd, value, target_client_id) */
static bool mpy_adapter_send(deepcraft_interface_t *self,
    uint8_t cmd, uint32_t value) {
    mpy_adapter_t *a = (mpy_adapter_t *)self;
    mp_obj_t dest[5];
    mp_load_method(a->py_transport, MP_QSTR_send, dest);
    dest[2] = mp_obj_new_int(cmd);
    dest[3] = mp_obj_new_int_from_uint(value);
    dest[4] = mp_obj_new_int(a->target_client_id);
    mp_call_method_n_kw(3, 0, dest);
    return true;
}

/*
 * vtable.register_receive_cb
 *   Stores the C callback then calls:
 *     transport.register_client(recv_client_id, mpy_adapter_receive_handler, ep_addr, ep_addr)
 *   so that incoming messages are routed through the shim into the C layer.
 */
static void mpy_adapter_register_cb(deepcraft_interface_t *self,
    void (*cb)(uint8_t cmd, uint32_t value)) {
    mpy_adapter_t *a = (mpy_adapter_t *)self;
    s_c_receive_cb = cb;

    mp_obj_t dest[6];
    mp_load_method(a->py_transport, MP_QSTR_register_client, dest);
    dest[2] = mp_obj_new_int(a->recv_client_id);
    dest[3] = MP_OBJ_FROM_PTR(&mpy_receive_shim_obj);
    dest[4] = mp_obj_new_int(a->recv_ep_addr);
    dest[5] = mp_obj_new_int(a->recv_ep_addr);
    mp_call_method_n_kw(4, 0, dest);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MicroPython event callback bridge
 *
 * The shared C model layer (deepcraft_engine.c) fires a C function pointer
 * callback.  mpy_model_event_cb translates that into a MicroPython call.
 * s_py_event_cb is protected from GC via MP_REGISTER_ROOT_POINTER.
 * ═══════════════════════════════════════════════════════════════════════════ */
static mp_obj_t s_py_event_cb;  /* initialised to mp_const_none in make_new */
MP_REGISTER_ROOT_POINTER(mp_obj_t deepcraft_py_event_cb);

static void mpy_model_event_cb(va_model_events_t event, uint32_t value) {
    if (s_py_event_cb == mp_const_none) {
        return;
    }
    mp_obj_t cb_args[2] = {
        mp_obj_new_int(event),
        mp_obj_new_int_from_uint(value),
    };
    mp_call_function_n_kw(s_py_event_cb, 2, 0, cb_args);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DEEPCRAFTModel MicroPython object
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef struct {
    mp_obj_base_t       base;
    deepcraft_engine_t   model;          /* shared C model state                  */
    interface_type_t    iface;          /* iface.IPC = &adapter.base             */
    mpy_adapter_t       adapter;        /* C vtable wrapping the MPY transport */
    mp_int_t            target_id;      /* target to enable (transport-specific)  */
} deepcraft_model_mpy_obj_t;

/* Singleton — one model instance per MicroPython context */
static deepcraft_model_mpy_obj_t *s_mpy_instance = NULL;
MP_REGISTER_ROOT_POINTER(mp_obj_t deepcraft_mpy_instance);

/* ── Constructor: DEEPCRAFTModel(interface, *, model=MODEL_VA, ...) ──────── */
static mp_obj_t deepcraft_model_mpy_make_new(const mp_obj_type_t *type,
    size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    enum {
        ARG_interface,
        ARG_model,
        ARG_target_client_id,
        ARG_recv_client_id,
        ARG_recv_ep_addr,
        ARG_target_id,
    };
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_interface,        MP_ARG_REQUIRED | MP_ARG_OBJ                                                   },
        { MP_QSTR_model,            MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEEPCRAFT_VA_MODEL}                     },
        { MP_QSTR_target_client_id, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEEPCRAFT_DEFAULT_TARGET_CLIENT_ID}    },
        { MP_QSTR_recv_client_id,   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEEPCRAFT_DEFAULT_RECV_CLIENT_ID}      },
        { MP_QSTR_recv_ep_addr,     MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEEPCRAFT_DEFAULT_RECV_EP_ADDR}        },
        { MP_QSTR_target_id,        MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEEPCRAFT_DEFAULT_TARGET_ID}           },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if (s_mpy_instance != NULL) {
        mp_raise_msg(&mp_type_RuntimeError,
            MP_ERROR_TEXT("Only one DEEPCRAFTModel instance is allowed"));
    }

    deepcraft_model_mpy_obj_t *self = mp_obj_malloc(deepcraft_model_mpy_obj_t, type);

    /* Wire up the C vtable adapter for the MicroPython transport object */
    self->adapter.base.send              = mpy_adapter_send;
    self->adapter.base.register_receive_cb = mpy_adapter_register_cb;
    self->adapter.py_transport        = args[ARG_interface].u_obj;
    self->adapter.target_client_id    = args[ARG_target_client_id].u_int;
    self->adapter.recv_client_id      = args[ARG_recv_client_id].u_int;
    self->adapter.recv_ep_addr        = args[ARG_recv_ep_addr].u_int;
    self->target_id                   = args[ARG_target_id].u_int;

    /* interface_type_t: IPC field points to our adapter vtable */
    self->iface.IPC = &self->adapter.base;

    /* Initialise model_type_t */
    model_type_t model_type;
    model_type.va_model = (va_model_t)args[ARG_model].u_int;

    /* Initialise MicroPython event callback sentinel */
    s_py_event_cb = mp_const_none;

    /* Initialise the transport (calls transport.init()) */
    mp_obj_t init_dest[2];
    mp_load_method_maybe(self->adapter.py_transport, MP_QSTR_init, init_dest);
    if (init_dest[0] != MP_OBJ_NULL) {
        mp_call_method_n_kw(0, 0, init_dest);
    }

    /* Init the shared C model layer — registers receive_dispatch via adapter */
    deepcraft_engine_init(&self->model, model_type, &self->iface);
    deepcraft_engine_set_event_cb(&self->model, mpy_model_event_cb);

    s_mpy_instance = self;
    MP_STATE_PORT(deepcraft_mpy_instance) = MP_OBJ_FROM_PTR(self);

    return MP_OBJ_FROM_PTR(self);
}

/* ── enable_target() → transport.enable_core(target_id) ─────────────────── */
static mp_obj_t deepcraft_model_mpy_enable_target(mp_obj_t self_in) {
    deepcraft_model_mpy_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_obj_t dest[3];
    mp_load_method(self->adapter.py_transport, MP_QSTR_enable_core, dest);
    dest[2] = mp_obj_new_int(self->target_id);
    mp_call_method_n_kw(1, 0, dest);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(deepcraft_model_mpy_enable_target_obj,
    deepcraft_model_mpy_enable_target);

/* ── start() — sends DEEPCRAFT_CMD_START via the shared C API ───────────── */
static mp_obj_t deepcraft_model_mpy_start(mp_obj_t self_in) {
    deepcraft_model_mpy_obj_t *self = MP_OBJ_TO_PTR(self_in);
    deepcraft_engine_start(&self->model);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(deepcraft_model_mpy_start_obj,
    deepcraft_model_mpy_start);

/* ── stop() — sends DEEPCRAFT_CMD_STOP via the shared C API ─────────────── */
static mp_obj_t deepcraft_model_mpy_stop(mp_obj_t self_in) {
    deepcraft_model_mpy_obj_t *self = MP_OBJ_TO_PTR(self_in);
    deepcraft_engine_stop(&self->model);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(deepcraft_model_mpy_stop_obj,
    deepcraft_model_mpy_stop);

/* ── set_event_cb(fn) — register MicroPython callback for VA events ──────────── */
static mp_obj_t deepcraft_model_mpy_set_event_cb(mp_obj_t self_in, mp_obj_t cb) {
    (void)self_in;
    if (cb != mp_const_none && !mp_obj_is_callable(cb)) {
        mp_raise_TypeError(MP_ERROR_TEXT("callback must be callable or None"));
    }
    s_py_event_cb = cb;
    MP_STATE_PORT(deepcraft_py_event_cb) = cb;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(deepcraft_model_mpy_set_event_cb_obj,
    deepcraft_model_mpy_set_event_cb);

/* ── state() → int (deepcraft_state_t value) ────────────────────────────── */
static mp_obj_t deepcraft_model_mpy_state(mp_obj_t self_in) {
    deepcraft_model_mpy_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(deepcraft_engine_get_state(&self->model));
}
static MP_DEFINE_CONST_FUN_OBJ_1(deepcraft_model_mpy_state_obj,
    deepcraft_model_mpy_state);

/* ═══════════════════════════════════════════════════════════════════════════
 * Type definition
 * ═══════════════════════════════════════════════════════════════════════════ */
static const mp_rom_map_elem_t deepcraft_model_locals_table[] = {
    /* Methods */
    { MP_ROM_QSTR(MP_QSTR_enable_target), MP_ROM_PTR(&deepcraft_model_mpy_enable_target_obj) },
    { MP_ROM_QSTR(MP_QSTR_start),         MP_ROM_PTR(&deepcraft_model_mpy_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop),          MP_ROM_PTR(&deepcraft_model_mpy_stop_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_event_cb),  MP_ROM_PTR(&deepcraft_model_mpy_set_event_cb_obj) },
    { MP_ROM_QSTR(MP_QSTR_state),         MP_ROM_PTR(&deepcraft_model_mpy_state_obj) },

    /* Model type constants */
    { MP_ROM_QSTR(MP_QSTR_MODEL_VA), MP_ROM_INT(DEEPCRAFT_VA_MODEL) },

    /* VA event constants (va_model_events_t) */
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_READY),             MP_ROM_INT(VA_EVENT_READY)             },
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_WAKEWORD_DETECTED), MP_ROM_INT(VA_EVENT_WAKEWORD_DETECTED) },
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_INTENT),            MP_ROM_INT(VA_EVENT_INTENT)            },
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_TIMEOUT),           MP_ROM_INT(VA_EVENT_TIMEOUT)           },
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_STOPPED),           MP_ROM_INT(VA_EVENT_STOPPED)           },
    { MP_ROM_QSTR(MP_QSTR_VA_EVENT_ERROR),             MP_ROM_INT(VA_EVENT_ERROR)             },

    /* State constants (deepcraft_state_t) */
    { MP_ROM_QSTR(MP_QSTR_STATE_IDLE),     MP_ROM_INT(DEEPCRAFT_STATE_IDLE)     },
    { MP_ROM_QSTR(MP_QSTR_STATE_RUNNING),  MP_ROM_INT(DEEPCRAFT_STATE_RUNNING)  },
    { MP_ROM_QSTR(MP_QSTR_STATE_STOPPING), MP_ROM_INT(DEEPCRAFT_STATE_STOPPING) },
    { MP_ROM_QSTR(MP_QSTR_STATE_ERROR),    MP_ROM_INT(DEEPCRAFT_STATE_ERROR)    },

    /* Protocol command constants */
    { MP_ROM_QSTR(MP_QSTR_CMD_START), MP_ROM_INT(DEEPCRAFT_CMD_START) },
    { MP_ROM_QSTR(MP_QSTR_CMD_STOP),  MP_ROM_INT(DEEPCRAFT_CMD_STOP)  },
};
static MP_DEFINE_CONST_DICT(deepcraft_model_locals_dict, deepcraft_model_locals_table);

MP_DEFINE_CONST_OBJ_TYPE(
    deepcraft_model_type,
    MP_QSTR_DEEPCRAFTModel,
    MP_TYPE_FLAG_NONE,
    make_new, deepcraft_model_mpy_make_new,
    locals_dict, &deepcraft_model_locals_dict);

/* ═══════════════════════════════════════════════════════════════════════════
 * Module
 * Can be later IFX_AI_MODEL and 3 classes VOICE_ASSISTANTModel, VISIONModel etc. if we add more models.
 * ═══════════════════════════════════════════════════════════════════════════ */
static const mp_rom_map_elem_t deepcraft_model_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),       MP_ROM_QSTR(MP_QSTR_deepcraft_model) },
    { MP_ROM_QSTR(MP_QSTR_DEEPCRAFTModel), MP_ROM_PTR(&deepcraft_model_type)    },
};
static MP_DEFINE_CONST_DICT(deepcraft_model_module_globals,
    deepcraft_model_module_globals_table);

const mp_obj_module_t mp_module_deepcraft_model = {
    .base    = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&deepcraft_model_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_deepcraft_model, mp_module_deepcraft_model);
