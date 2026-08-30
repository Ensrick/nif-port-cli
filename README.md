# nif-port-cli

A small, non-interactive NIF inspection and Skyrim LE-to-SE conversion wrapper around
the current `nifly` source vendored by `ousnius/SSE-NIF-Optimizer`.

It is intentionally fail-closed: files containing unknown blocks are not converted,
and converted files are reloaded and checked for SSE stream version 100 and SSE-safe
geometry before the command succeeds. Conversion also strips invalid control
characters from embedded texture paths; one source mesh in Lost LongSwords contains
a trailing newline in a shader texture reference.

The tool does not contain or redistribute mod assets.

`remap-textures` writes a new NIF with selected shader texture paths changed.
It never overwrites its input or an existing output, rejects unknown blocks,
requires every requested source path to match, and reloads the result before
success. This makes narrow, local-only asset overlays reproducible without
editing an installed vendor mod in place:

```powershell
nif-port-cli remap-textures input.nif output.nif `
  'textures\shared\old.dds' 'textures\my-overlay\unique.dds'
```

## Build

Clone recursively so the GPL-3.0-licensed `nifly` dependency is checked out at
the audited revision:

```bash
git clone --recursive https://github.com/Ensrick/nif-port-cli.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This wrapper is distributed under GPL-3.0 because it links with `nifly`.
