# DEEPCRAFT™ Model Interface

A small, transport-agnostic interface layer that exposes Infineon's DEEPCRAFT™
Voice Assistant (VA) model to MicroPython on multi-core PSOC™ Edge devices
(with CM33 running MicroPython, CM55 running the model).

## What it does

- Provides a shared C engine that drives the VA model and translates raw wire commands into
  high-level events (ready, wake-word, intent, timeout, stopped, error).
- Keeps the model logic **transport-agnostic**: any inter-core link (IPC pipe,
  UART, OpenAMP, …) could be plugged in by implementing a small vtable, so no
  transport-specific code lives in the engine. Currently only IPC is enabled and tested in this framework.
- Exposes the engine to Python as a `deepcraft_model` C module. The caller can
  passes their own transport object (e.g. `machine.IPC`) and registers an event
  callback to react to VA events.

## Components

| File | Role |
| --- | --- |
| [deepcraft/deepcraft_interface.h](deepcraft/deepcraft_interface.h) | Shared API: types, wire commands, transport vtable, engine declarations. |
| [deepcraft/deepcraft_engine.c](deepcraft/deepcraft_engine.c) | Transport-agnostic model state machine and event dispatch. |
| [deepcraft/deepcraft_interface.c](deepcraft/deepcraft_interface.c) | MicroPython `deepcraft_model` C module; adapts a Python transport to the engine. |
| [deepcraft_interface.mk](deepcraft_interface.mk) | Build fragment for the including projects. |

## Build integration

Include the makefile fragment in your build:

```makefile
include path/to/deepcraft_interface.mk
```

- **CM33 / MicroPython port:** compiles the engine and module sources and adds
  the header include path.
- **CM55 / ModusToolbox™:** only the shared header is pulled in; the model API
  and IPC implementation live in the CM55 project itself.

## License

MIT © 2026 Infineon Technologies AG. See [LICENSE](LICENSE).
