/**
 * @file deepcraft_interface.h
 * @brief Shared DEEPCRAFT model interface layer.
 *
 * @copyright Copyright (c) 2026 Infineon Technologies AG
 * @license SPDX-License-Identifier: MIT
 */

#ifndef DEEPCRAFT_INTERFACE_H
#define DEEPCRAFT_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @defgroup deepcraft_model DeepCraft Model Interface
 * @brief Shared transport-agnostic model layer for DeepCraft VA models.
 * @{
 */

/* ── Types ──────────────────────────────────────────────────────────────── */

/**
 * @brief VA model type tag.
 *
 * Identifies which DeepCraft model is active.  Pass inside a @ref model_type_t
 * to deepcraft_engine_init().
 */
typedef enum {
    DEEPCRAFT_VA_MODEL = 0, /**< Voice Assistant model */
    /* Future: DEEPCRAFT_STUDIO_MODEL, DEEPCRAFT_READY_MODEL, … */
} va_model_t;

/**
 * @brief Model container struct.
 *
 * Holds the active model type tag.  Extensible: additional model-specific
 * fields can be added alongside @p va_model in the future.
 */
typedef struct {
    va_model_t va_model; /**< Active model type */
    /* Future model type fields go here : version, specific capabilty?*/
} model_type_t;

/**
 * @brief VA model event codes, decoupled from wire bytes.
 *
 * These values are passed to the user event callback registered via
 * deepcraft_engine_set_event_cb() / DEEPCRAFTModel.set_event_cb().
 */
typedef enum {
    VA_EVENT_READY             = 0, /**< VA initialised and ready to listen        */
    VA_EVENT_WAKEWORD_DETECTED = 1, /**< Wake-word detected; listening for command */
    VA_EVENT_INTENT            = 2, /**< Intent recognised; @p value = intent index */
    VA_EVENT_TIMEOUT           = 3, /**< Command listen window timed out           */
    VA_EVENT_STOPPED           = 4, /**< VA stopped (acknowledgement of CMD_STOP)  */
    VA_EVENT_ERROR             = 5, /**< Fatal VA error                            */
} va_model_events_t;

/* ── Protocol wire commands ─────────────────────────────────────────────── */

/** @defgroup deepcraft_cmds Wire Protocol Commands
 *  @{
 */
/** @brief Host → Target: start the Voice Assistant. */
#define DEEPCRAFT_CMD_START                 (0x82U)
/** @brief Host → Target: stop the Voice Assistant. */
#define DEEPCRAFT_CMD_STOP                  (0x83U)

/** @brief Target → Host: VA initialised and ready to listen. */
#define DEEPCRAFT_CMD_VA_READY              (0xA0U)
/** @brief Target → Host: wake-word detected. */
#define DEEPCRAFT_CMD_VA_WAKEWORD_DETECTED  (0xA2U)
/** @brief Target → Host: command listen window timed out. */
#define DEEPCRAFT_CMD_VA_TIMEOUT            (0xA3U)
/** @brief Target → Host: VA stopped (ack to CMD_STOP). */
#define DEEPCRAFT_CMD_VA_STOPPED            (0xA4U)
/** @brief Target → Host: fatal VA error. */
#define DEEPCRAFT_CMD_VA_ERROR              (0xE1U)
/** @} */ /* end of deepcraft_cmds */

/* ── Transport vtable ───────────────────────────────────────────────────── */

/**
 * @brief Transport vtable — plug-in point for any inter-core link.
 *
 * Implement this struct for your transport (IPC, UART, OpenAMP etc.) and embed it as
 * the **first** member of your transport-specific struct so it can be safely
 * cast:
 * @code{.c}
 *   typedef struct {
 *       deepcraft_interface_t base;   // MUST be first
 *       // transport-specific fields …
 *   } transport_t;
 * @endcode
 */
typedef struct deepcraft_interface_s deepcraft_interface_t;

struct deepcraft_interface_s {
    /**
     * @brief Send a command/event to the other core.
     *
     * @param self  Pointer to this interface instance.
     * @param cmd   @ref deepcraft_cmds constant or raw intent index byte.
     * @param value Optional payload; pass @c 0 if unused.
     * @return @c true on success, @c false if the transport is busy/unavailable.
     */
    bool (*send)(deepcraft_interface_t *self, uint8_t cmd, uint32_t value);

    /**
     * @brief Register a callback for incoming messages.
     *
     * @param self Pointer to this interface instance.
     * @param cb   Callback invoked on every received message.
     *             @warning May be called from interrupt/transport context;
     *                      keep the implementation minimal.
     */
    void (*register_receive_cb)(deepcraft_interface_t *self,
        void (*cb)(uint8_t cmd, uint32_t value));
};

/**
 * @brief Interface structure.
 *
 * Holds the active transport vtable pointer(s).  Populate @p IPC (or a
 * future field) before passing to deepcraft_engine_init().
 */
typedef struct {
    deepcraft_interface_t *IPC; /**< IPC-pipe transport (default) */
} interface_type_t;

/* ── Model runtime ──────────────────────────────────────────────────────── */

/**
 * @brief Internal model state machine states.
 */
typedef enum {
    DEEPCRAFT_STATE_IDLE     = 0, /**< No VA session active; waiting for start */
    DEEPCRAFT_STATE_RUNNING  = 1, /**< VA session active                       */
    DEEPCRAFT_STATE_STOPPING = 2, /**< Stop sent; waiting for STOPPED ack      */
    DEEPCRAFT_STATE_ERROR    = 3, /**< Fatal error; requires re-initialisation */
} deepcraft_state_t;

/**
 * @brief Model runtime object.
 *
 * Allocate one per model session (typically a single static instance).
 * Initialise with deepcraft_engine_init() before calling any other function.
 */
typedef struct {
    model_type_t         model_type; /**< Active model type                   */
    interface_type_t    *interface;  /**< Configured transport interface      */
    deepcraft_state_t    state;      /**< Current state machine state         */
    /** @brief User event callback; set via deepcraft_engine_set_event_cb(). */
    void (*on_event)(va_model_events_t event, uint32_t value);
} deepcraft_engine_t;

/* ── API ─────────────────────────────────────────────────────────────────── */

/**
 * @brief Initialise the model object and register the receive dispatcher.
 *
 * Registers an internal receive dispatcher with the transport so that
 * incoming wire bytes are automatically translated to @ref va_model_events_t
 * and forwarded to the user callback.  Must be called before
 * deepcraft_engine_start() / deepcraft_engine_stop().
 *
 * @param self       Model instance to initialise.
 * @param model_type Type of model to activate (e.g. @ref DEEPCRAFT_VA_MODEL).
 * @param iface      Configured transport interface; @p iface->IPC must not be NULL.
 */
void deepcraft_engine_init(deepcraft_engine_t *self,
    model_type_t model_type, interface_type_t *iface);

/**
 * @brief Register the user callback invoked on every VA event.
 *
 * @param self Model instance.
 * @param cb   Callback with signature @c void cb(va_model_events_t event, uint32_t value).
 *             Pass @c NULL to deregister.
 */
void deepcraft_engine_set_event_cb(deepcraft_engine_t *self,
    void (*cb)(va_model_events_t event, uint32_t value));

/**
 * @brief Send @ref DEEPCRAFT_CMD_START to the target and transition to RUNNING.
 *
 * @param self Model instance.
 */
void deepcraft_engine_start(deepcraft_engine_t *self);

/**
 * @brief Send @ref DEEPCRAFT_CMD_STOP to the target and transition to STOPPING.
 *
 * The state transitions to @ref DEEPCRAFT_STATE_IDLE once the target sends
 * @ref DEEPCRAFT_CMD_VA_STOPPED.
 *
 * @param self Model instance.
 */
void deepcraft_engine_stop(deepcraft_engine_t *self);

/**
 * @brief Return the current state of the model.
 *
 * @param self Model instance (const).
 * @return Current @ref deepcraft_state_t value.
 */
deepcraft_state_t deepcraft_engine_get_state(const deepcraft_engine_t *self);

/**
 * @brief Process an incoming raw wire command.
 *
 * Called by the transport layer (via the registered receive dispatcher) or
 * directly from test code.  Updates the internal state machine then translates
 * the byte to a @ref va_model_events_t and invokes the user callback.
 *
 * @note Intent commands: any @p cmd byte that does not match a named
 *       @ref deepcraft_cmds constant is treated as an intent index and
 *       dispatched as @ref VA_EVENT_INTENT with @p value set to @p cmd.
 *
 * @param self  Model instance.
 * @param cmd   Received wire byte (@ref deepcraft_cmds or intent index).
 * @param value Optional payload associated with the command.
 */
void deepcraft_engine_on_receive(deepcraft_engine_t *self,
    uint8_t cmd, uint32_t value);

/** @} */ /* end of deepcraft_model group */

#endif /* DEEPCRAFT_INTERFACE_H */


