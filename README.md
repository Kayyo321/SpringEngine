# SpringEngine

A new idea for how video game engines should be.

## C Project Build System

Project layout:

- `src/` → C source and header files
- `lib/` → prebuilt libraries (`.a`, `.so`, `.dylib`) to link (recursive, static-first)
- `bin/` → build output binaries

### Commands

- Build: `make`
- Rebuild: `make re`
- Clean all object files (`.o`): `make clean`
- Clean objects + binary: `make fclean`

By default, the binary is generated at `bin/springengine`.
