# AGENTS.md

This project is a WIP implementation of the CoAP protocol in modern C++ (C++20). It especially targets small embedded systems (for example STM32 Cortex-M MCU series) in a functional safety context.

## Code style

- Google C++ Style Guide
- Do not use the heap, but rely on memory pools if dynamic allocation is required
- Do not use platform-dependant (e.g. WinAPI or POSIX) code in core components (only in interface implementations)
- Use the convention `<name>IF` for interfaces