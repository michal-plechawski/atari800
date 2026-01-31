# artifacting-c-lib

Note: Sources were moved into `src/altirra_artifacting/` for direct integration
with atari800. This directory is kept only for historical reference.

This directory provides a pure C port of the artifacting core.

Notes:
- The API surface is C and can be included from C or other languages.
- This port is scalar-only (no SSE/NEON).
- PAL high artifacting, NTSC artifacting (low/high), and PAL YRGB blending are implemented; other artifacting paths currently fall back to non-artifacting output.
- Frame blending (copy/exchange, linear, mono persistence) is implemented in scalar.
- Color matching matrices are ignored; only the built-in PAL/NTSC defaults are fully supported.

Build (Makefile):
- `make -C src-lib/artifacting-c-lib`

Build (CMake):
- `cmake -S src-lib/artifacting-c-lib -B src-lib/artifacting-c-lib/build`
- `cmake --build src-lib/artifacting-c-lib/build`
