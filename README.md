# nif-port-cli

A small, non-interactive NIF inspection and Skyrim LE-to-SE conversion wrapper around
the current `nifly` source vendored by `ousnius/SSE-NIF-Optimizer`.

It is intentionally fail-closed: files containing unknown blocks are not converted,
and converted files are reloaded and checked for SSE stream version 100 and SSE-safe
geometry before the command succeeds. Conversion also strips invalid control
characters from embedded texture paths; one source mesh in Lost LongSwords contains
a trailing newline in a shader texture reference.

FaceGen and other head-part meshes require the engine's dynamic geometry format.
Use the explicit head-parts mode only for head, eye, mouth, brow, hair, and FaceGen
NIFs. The command verifies after reload that every output shape is a
`BSDynamicTriShape`:

```powershell
nif-port-cli convert-sse --headparts le-facegen-folder se-facegen-folder
```

For Oldrim FaceGen whose eyes must follow the Special Edition shader
convention, add `--se-eye-shaders`. Eye-data shapes are normalized from the
Oldrim eye shader/flag pair to the SE environment-map pair, and the saved file
is reloaded and checked before success:

```powershell
nif-port-cli convert-sse --headparts --se-eye-shaders le-facegen-folder se-facegen-folder
```

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

`inspect` reports each shape's name, vertex and triangle counts, skin bones,
texture paths, lighting shader type and flags, and whether SSE packed eye data
is present. The latter fields make head-part and eye conversions auditable
without opening NifSkope. `clone-shape` copies one unambiguous named shape and its
dependent blocks from a donor into a separate base NIF, then reloads the
result and verifies the cloned geometry, skin-bone list, and textures. It is
intended for narrow compatibility overlays where an independently weighted
decoration must be combined with an existing body refit:

```powershell
nif-port-cli clone-shape cbbe-outfit.nif vendor-outfit.nif `
  'VendorOutfit:Fur' output.nif
```

`export-obj` creates a deterministic, geometry-only inspection export. It is
not a game-asset converter; its purpose is static visual QA and exact geometry
comparisons between an input and an owned overlay.

## Build

Clone recursively so the GPL-3.0-licensed `nifly` dependency is checked out at
the audited revision:

```bash
git clone --recursive https://github.com/Ensrick/nif-port-cli.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

This wrapper is distributed under GPL-3.0 because it links with `nifly`.
