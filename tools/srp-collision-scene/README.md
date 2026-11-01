# Skyrim collision scene producer (tooling)

This directory holds the offline producer that turns **real Skyrim Special
Edition assets** into the audited scene document that `SrpCollision::Scene::Load`
accepts. Nothing here invents geometry: every triangle comes from a shipped
Bethesda mesh, and anything that cannot be decoded is reported as `unresolved`
so `Scene::Trace` returns `Unknown` and the impact is refused.

## Implemented

- `bsa.py` — Bethesda BSA v105 reader (Skyrim SE). Verified against
  `Skyrim - Meshes0.bsa`: 19443 files, 18862 NIFs.
- `lz4.py` — LZ4 frame decoder. SSE archives are LZ4-framed, **not** zlib; this
  is the single most common way a v105 reader fails silently.
- `nif.py` — NIF 20.2.0.7 reader plus `bhkCompressedMeshShapeData` decoding to
  object-space triangles.
- `selfcheck.py` — proves the readers against real data.

## Format notes that cost real time to establish

- BSA v105: 36-byte header, **24-byte** folder records (v104 is 16), folder
  names interleaved with their file records, and a trailing block of
  NUL-terminated file names.
- Compression is declared by the archive flags and the per-file size bit is a
  *toggle*, not an absolute: an entry is `[u32 original size][LZ4 frame]`.
- NIF: header line, version, endian, user version, block count, the Bethesda
  stream header (bsver plus three `u8`-prefixed export strings), block type
  table, type indices, block sizes, string table, groups, blocks, and a
  trailing root list.
- Block sizes are only trusted when blocks **plus the trailing root list** tile
  the payload exactly; a file that fails that check is rejected instead of being
  parsed at a guessed offset.
- `bhkCompressedMeshShapeData`: four `u32` header words, `f32` error, two
  `Vector4` bounds, welding/material bytes, three link arrays, materials,
  transforms, big vertices/triangles, and chunks. Chunk vertex components are
  `u16` triples scaled by 1/1000 and offset by the chunk (and optional
  transform) translation. Strip lengths yield `L-2` triangles each; leftover
  indices form an explicit triangle list.

## Usage

```bash
python tools/srp-collision-scene/selfcheck.py [--bsa <path to Skyrim - Meshes0.bsa>]
```

Exit codes: `0` pass, `2` game data not available (skip), `1` failure.

## Policy

- Synthetic geometry, NAVM data or an empty box are never substituted for
  physical collision.
- A reference whose collision is unsupported (convex hulls, phantoms, missing
  meshes) is emitted as `unresolved` rather than silently treated as empty
  space.
- Coverage is only marked complete for spaces where every reference was
  enumerated. Exterior terrain collision is not produced by this tool, so
  exteriors must not claim complete coverage.
