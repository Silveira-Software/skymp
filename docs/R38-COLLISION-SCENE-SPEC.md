# Binary format specification — Skyrim SE: BSA v105, NIF 20.2.0.7, bhkCompressedMeshShapeData

Scope: TES V: Skyrim Special Edition. NIF version `20.2.0.7` (`0x14020007`), user version `12`,
Bethesda game, Havok 2010. All integers little-endian unless the Xbox360 flag says otherwise.
`f32` = IEEE-754 binary32, 4 bytes.

Sources are listed at the end; every claim below is traceable to at least one working
implementation (NifSkope reads/renders SSE CMS; nifly/PyNifly round-trips SSE NIFs; xEdit and
bsatk read SSE BSAs).

---

## 0. Three corrections you must apply before writing code

These contradict premises in the request. They are not stylistic — each one silently corrupts data.

1. **BSA v105 does not use zlib.** Skyrim SE archives are compressed with **LZ4 frame format**.
   Both xEdit (`ctLZ4Frame` for `HEADER_VERSION_SSE`) and bsatk (`TYPE_SKYRIMSE` →
   `LZ4F_decompress`) use LZ4F, not inflate. There is **no wbits value** for v105. zlib is correct
   only for v103/v104 (Oblivion, FO3/FNV, Skyrim LE).
2. **There is no `maskVertex` field in `bhkCompressedMeshShapeData` for 20.2.0.7.** The block
   starts with exactly four `uint32` + one `f32`. Inserting a fifth `uint32` shifts the whole block
   by 4 bytes and produces garbage bounds/materials.
3. **`maskWIndex` precedes `maskIndex`** — see §3.1 for the two independent confirmations and a
   self-validating check.
4. (minor) `bhkCompressedMeshShape` has **no transform and no welding type**; both live elsewhere
   (§3.6). And the NIF header order is `Num Strings`, **then** `Max String Length`, then the
   strings themselves (§2).

---

## 1. BSA archive, version 105 (SSE)

### 1.1 Header — 36 bytes

| Offset | Type | Field | Value / meaning |
|---:|---|---|---|
| 0 | `char[4]` | fileId | `"BSA\0"` = `42 53 41 00` (as LE u32: `0x00415342`) |
| 4 | `u32` | version | `105` (`0x69`). (v103 = Oblivion, `0x67`; v104 = FO3/FNV/Skyrim LE, `0x68`) |
| 8 | `u32` | offset | Absolute offset of the first folder record. Always `36` in practice; do not hardcode — seek here. |
| 12 | `u32` | archiveFlags | see §1.2 |
| 16 | `u32` | folderCount | number of folder records |
| 20 | `u32` | fileCount | total number of file records (== total files) |
| 24 | `u32` | totalFolderNameLength | sum of all folder-name lengths **including the NUL**, **excluding** the 1-byte length prefix |
| 28 | `u32` | totalFileNameLength | sum of all file-name lengths including the NUL (file names are stored as C strings) |
| 32 | `u16` | fileFlags | see §1.2 (hint only) |
| 34 | `u16` | padding | 2 bytes of padding — this is why the header is 36 bytes, not 34 |

xEdit: `FoldersOffset := SizeOf(magic) + SizeOf(version) + SizeOf(header)` = 4+4+24 = 36, where its
`HeaderTES4` record is `Flags, FolderCount, FileCount, TotalFolderNameLength, TotalFileNameLength,
FileFlags(u16), Padding(u16)` = 24 bytes.

### 1.2 Flag bits

`archiveFlags` (u32):

| Bit | xEdit name | Meaning |
|---:|---|---|
| `0x001` | `ARCHIVE_PATHNAMES` | Directory (folder) names are present. Structural. |
| `0x002` | `ARCHIVE_FILENAMES` | File names are present. Structural. |
| `0x004` | `ARCHIVE_COMPRESS` | **Default** compression state for files (see §1.6) |
| `0x008` | `ARCHIVE_RETAINDIR` | No structural effect |
| `0x010` | `ARCHIVE_RETAINNAME` | No structural effect |
| `0x020` | `ARCHIVE_RETAINFOFF` | No structural effect |
| `0x040` | `ARCHIVE_XBOX360` | All numbers and hashes are **big-endian**, and the hash algorithm differs |
| `0x080` | `ARCHIVE_STARTUPSTR` | No structural effect |
| `0x100` | `ARCHIVE_EMBEDNAME` | Each data block is prefixed with a `bstring` full path (§1.7) |
| `0x200` | `ARCHIVE_XMEM` | XMem codec (Xbox360 only, requires `0x004`) |
| `0x400` | `ARCHIVE_UNKNOWN10` | Emitted by xEdit when writing; no known effect |

`fileFlags` (u16 in the header, and a `u32` in each folder record): purely advisory content-type
hints. **Sources disagree on the naming** — UESP gives Menus/Sounds/Voices/Shaders/Trees/Fonts,
xEdit's constants are extension-oriented:

| Bit | UESP (v104-era naming) | xEdit constant |
|---:|---|---|
| `0x001` | Meshes | `FILE_NIF` |
| `0x002` | Textures | `FILE_DDS` |
| `0x004` | Menus | `FILE_XML` |
| `0x008` | Sounds | `FILE_WAV` |
| `0x010` | Voices | `FILE_MP3` |
| `0x020` | Shaders | — |
| `0x040` | Trees | — |
| `0x080` | Fonts | — |
| `0x100` | Miscellaneous | `FILE_MISC` |

Do not branch on these when parsing structure.

### 1.3 Folder record — **24 bytes in v105** (16 bytes in v104)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u64` | nameHash |
| 8 | `u32` | count (files in this folder) |
| 12 | `u32` | unknown / padding |
| 16 | `u64` | offset |

**Sources disagree on the tail of this record**, and both readings consume 24 bytes:

* xEdit (`baSSE` branch) reads `hash:u64, fileCount:u32, Unk32:u32, Offset:i64` → 8+4+4+8 = 24.
* bsatk `Folder::readFolderSE` reads `hash:u64, count:u32, skip:u32, offset:u64` → 24. It writes
  bit-for-bit the same layout.
* UESP describes it as `hash:u64, count:u32, padding:u32, offset:u32, padding:u32` → 24, i.e. the
  offset is a 4-byte field at the same byte position 16, followed by 4 bytes of padding.

So the **offset field starts at byte 16 either way**; only its width and the trailing 4 bytes are in
dispute. The layout used by code that works with real SSE archives is the 24-byte skip with the
`u64` at 16. `offset` is `absolute_position_of_this_folder's_name + totalFileNameLength`; bsatk
recovers the position with `seek(offset - totalFileNameLength)`. **A sequential reader needs none of
this** (see §1.5) — that is the recommended approach.

### 1.4 File record — 16 bytes (both v104 and v105)

| Offset | Type | Field |
|---:|---|---|
| 0 | `u64` | nameHash |
| 8 | `u32` | size (bit-packed, §1.6) |
| 12 | `u32` | offset (absolute, from byte 0 of the archive, to the start of the data block) |

### 1.5 Sequential layout (recommended read order)

```
header                                  (36 bytes)
folderCount × folder record             (24 bytes each, v105)
for each folder, in the same order:
    bzstring  folderName                (u8 len, then len bytes, NO NUL)
    count × file record                 (16 bytes each)
fileCount × cstring  fileName           (NUL-terminated, no length prefix)
fileCount × data block                  (at file.offset, in any order)
```

Folder names are lower-case ASCII with `\` separators; file names in the last block are the
basenames only (`cuirass.nif`), and the full path is `foldername + "\" + filename`.

* `bzstring` = `u8` length followed by that many bytes. The length does **not** include a NUL and no
  NUL is written (`totalFolderNameLength` still counts a NUL per name, so
  `sum(len) + folderCount == totalFolderNameLength`).
* Folder records and file records are sorted by hash, and folders must be contiguous — irrelevant for
  reading, since all names are stored. You never need the TES4 hash algorithm to *read* an archive.

### 1.6 Per-file compression — exact rule

```
raw      = file_record.size
toggle   = (raw & 0x40000000) != 0     # bit 30; bsatk masks 0xC0000000 to also swallow bit 31
stored   = raw & 0x3FFFFFFF            # 0x3fffffff
compressed = toggle XOR ((archiveFlags & 0x004) != 0)
```

The bit is a **toggle relative to the archive default**, not an absolute "is compressed" flag:
if the archive is default-compressed (`0x004` set) a set bit 30 means *this file is stored
uncompressed*; if the archive is default-uncompressed a set bit 30 means *this file is compressed*.
xEdit implements exactly this as `(ArchiveFlags and ARCHIVE_COMPRESS <> 0) xor (Size and
FILE_SIZE_COMPRESS <> 0)`; bsatk stores it as `m_ToggleCompressed` with the documented meaning
"true if its compression mode for this file differs from the archive default". bsatk never *writes*
bit 31 but masks it on read, so mask `0xC0000000` for robustness.

Data block at `file.offset`, total length `stored`:

| Order | Field |
|---|---|
| 1 | if `archiveFlags & 0x100`: `bstring` (u8 len + len bytes, no NUL) full path |
| 2 | if `compressed`: `u32` originalSize (**the first 4 bytes are the uncompressed size** — yes) |
| 3 | payload: `stored - (name_len+1 if embedded) - (4 if compressed)` bytes |

Then:

* **v105 (SSE): payload is an LZ4 frame.** Feed it to `LZ4F_decompress` (or `lz4.frame` in Python)
  with output length = `originalSize`. LZ4 frame magic = `04 22 4D 18` (`0x184D2204`).
* **v104 and earlier: payload is a zlib stream.** `zlib.decompress(payload)` with the default
  `wbits=15` works; use `wbits=15+32` (=47) if you want auto-detection of zlib *and* gzip headers —
  that is literally what both NifSkope (`inflateInit2(&strm, 15 + 32)`) and bsatk
  (`inflateInit2(&stream, 15 + 32)`) do. Negative wbits (`-15`, raw deflate) is **wrong**.
* Robustness: sniff instead of trusting the version. `payload[0:4] == 04 22 4D 18` → LZ4 frame;
  `(payload[0] & 0x0F) == 8 and ((payload[0] << 8) | payload[1]) % 31 == 0` → zlib. Some
  third-party tools have produced mismatched v105/zlib archives.
* Validate `originalSize` (`< 2 GiB`, and `> 0` when `stored > 4`) before allocating.

---

## 2. NIF 20.2.0.7 / user version 12 header

Order is fixed. `SizedString` = `u32 length` + `length` bytes, **not** NUL-terminated.
`ExportString` = `u8 length` + `length` bytes where the NUL **is** included in the length.

| # | Type | Field | Notes for 20.2.0.7 / uv 12 |
|---:|---|---|---|
| 1 | ASCII line | Header string | `"Gamebryo File Format, Version 20.2.0.7\n"` — **39 bytes total**, read up to *and including* `0x0A`. **Not** length-prefixed. |
| 2 | `u32` | version | `0x14020007` |
| 3 | `u8` | endianness | `1` = little-endian (`0` = big). Present since 20.0.0.3 |
| 4 | `u32` | user version | `12` |
| 5 | `u32` | num blocks | |
| 6 | `u32` | **BS version** (`bsver`) | Present for 20.2.0.7 when user version ≥ 3 (nif.xml token `#BSSTREAMHEADER#`; nifly `IsBethesda()` = `file == V20_2_0_7 && user >= 11`). Skyrim-era value is 83. |
| 7 | `ExportString` | author | `u8` len (incl NUL) + bytes |
| 8 | `ExportString` | process script | ditto |
| 9 | `ExportString` | export script | ditto |
| — | `ExportString` | max filepath | **only if `bsver >= 103`** → **not present in SSE**. nifly adds a 4th string when `bsver == 130`. |
| 10 | `u16` | num block types | |
| 11 | `SizedString[num block types]` | block type names | `u32` len + chars, **no NUL**. (Not `u16`! An off-by-two here is the classic bug.) |
| 12 | `u16[num blocks]` | block type index | index into (11). **Mask with `0x7FFF`** — bit 15 is a PhysX-related flag that is set on some blocks. |
| 13 | `u32[num blocks]` | block sizes | byte size of each block's payload |
| 14 | `u32` | num strings | |
| 15 | `u32` | max string length | **before** the strings, not after |
| 16 | `SizedString[num strings]` | string table | |
| 17 | `u32` | num groups | normally 0 |
| 18 | `u32[num groups]` | groups | omitted when 17 == 0 |

Then the blocks, back to back. **Footer** after the last block (present for version ≥ 3.3.0.13, so
present for SSE): `u32 numRoots` + `numRoots × u32` root block indices — normally `1` and `0`.

`max string length` is derived (max length over the string table, some writers include the NUL). It
is **informational only** — never use it to size a read; the per-string `u32` length is authoritative.

**Are block sizes trustworthy for random access? No.** They are a *hint*:

* NifSkope's loader has a user setting `"Ignore Block Size"` whose **default is `true`**
  (`settings.value("Ignore Block Size", true)`), i.e. by default it parses each block sequentially
  and ignores the size array entirely.
* When sizes are used (`ignoreSize == false`), NifSkope compares the post-parse stream position
  against `start + size` and, on mismatch, logs
  `"device position incorrect after block number N ... expected 0x..."` and **seeks past the block**.
  It also uses the size to recover from a failed block parse.
* nifly only uses them to skip block types it cannot parse (`NiUnknown(stream, GetBlockSize(i))`).

Practical rule: parse sequentially, building your own offset table in one pass; use the declared
sizes only to (a) skip unknown block types and (b) sanity-check. Do not `seek(offset += size)` as
your primary strategy — a single wrong size in a third-party file desynchronises everything.

---

## 3. `bhkCompressedMeshShapeData` (Skyrim / SSE)

Verified against nif.xml `niobject name="bhkCompressedMeshShapeData"` and nifly
`bhkCompressedMeshShapeData::Sync` — they agree field for field.

Total fixed header before the first array: **54 bytes**.

### 3.1 Header

| Offset | Type | Size | Field | Typical |
|---:|---|---:|---|---|
| 0 | `u32` | 4 | bitsPerIndex | `17` |
| 4 | `u32` | 4 | bitsPerWIndex | `18` |
| 8 | `u32` | 4 | **maskWIndex** | `0x0003FFFF` |
| 12 | `u32` | 4 | **maskIndex** | `0x0001FFFF` |
| 16 | `f32` | 4 | error | `0.001` |
| 20 | `f32[4]` | 16 | AABB min (Vector4) | |
| 36 | `f32[4]` | 16 | AABB max (Vector4) | |
| 52 | `u8` | 1 | welding type | `0` = `ANTICLOCKWISE` |
| 53 | `u8` | 1 | material type | `1` = `SINGLE_VALUE_PER_CHUNK` |
| 54 | — | — | *(arrays begin)* | |

**`maskWIndex` before `maskIndex` — confirmed twice, and self-validating.**

* nif.xml order in the `<niobject>`: `Bits Per Index, Bits Per W Index, Mask W Index, Mask Index, Error, AABB, Welding Type, Material Type, …`
* nifly `Sync()`: `bitsPerIndex, bitsPerWIndex, maskWIndex, maskIndex, error, aabbBoundMin, aabbBoundMax, weldingType, materialType, …`

NifSkope's CMS renderer (`drawCMS`) loads these files through nif.xml, and nifly is the library
behind PyNifly/BodySlide; both work on real SSE meshes. The layours that put `maskIndex` first are
wrong.

Self-check that costs one line and catches the swap: the masks are the low-bit masks of the
corresponding widths, so

```
assert maskIndex  == (1 << bitsPerIndex)  - 1     # 0x1FFFF
assert maskWIndex == (1 << bitsPerWIndex) - 1     # 0x3FFFF
assert maskWIndex > maskIndex
```

If you read `0x1FFFF` at offset 8 you have the two fields swapped (and your AABB floats will be
denormal garbage).

**No `maskVertex` field exists here.** If a reference lists five `u32`s before `error`, it is not
describing 20.2.0.7 — dropping it is mandatory. `bitsPerIndex`/`maskIndex` and
`bitsPerWIndex`/`maskWIndex` describe the bit allocation of Havok's runtime *shape key* (nif.xml:
"Number of bits in the shape-key reserved for a triangle index" / "… for a triangle index **and its
winding**"), i.e. a contact-point → triangle lookup value. **They play no role in decoding vertex
positions**, and no working SSE decoder uses them for geometry. A `w` component does not appear in
the stored vertex data either — chunk vertices are three `u16`, not four.

### 3.2 Materials arrays

Note the order: the three legacy `u32` tables come *before* the real material table, and
`numNamedMaterials` comes *after* it.

| Type | Field |
|---|---|
| `u32` | numMaterials32 |
| `u32[numMaterials32]` | materials32 (unused) |
| `u32` | numMaterials16 |
| `u32[numMaterials16]` | materials16 (unused) |
| `u32` | numMaterials8 |
| `u32[numMaterials8]` | materials8 (unused) |
| `u32` | **numMaterials** |
| `numMaterials × 8 bytes` | **materials** (see below) |
| `u32` | numNamedMaterials (unused; `hkpNamedMeshMaterial`, no array is written) |

Material entry — `bhkMeshMaterial`, 8 bytes, layout fixed by `bhkMeshMaterial size="8"` in nif.xml:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | material (`SkyrimHavokMaterial`) |
| 4 | `u8` | layer (`SkyrimLayer`) |
| 5 | `u8` | flags (`CollisionFilterFlags`) |
| 6 | `u16` | group |

`numMaterials` is an explicit `u32` count — it is **not** derivable from anything else. In vanilla
SSE data it is usually 1. Chunks index this table via `chunk.materialIndex`.

### 3.3 Chunk transforms

| Type | Field |
|---|---|
| `u32` | numTransforms |
| `numTransforms × 32 bytes` | transforms |

`bhkQsTransform`, 32 bytes (the scale vector is **not** serialised):

| Offset | Type | Field |
|---:|---|---|
| 0 | `f32[4]` | translation (Vector4; `w` unused) |
| 16 | `f32[4]` | rotation, quaternion **x, y, z, w** |

`numTransforms` is normally 1 and the single entry is identity. Chunks index it via
`chunk.transformIndex`; **treat out-of-range indices as "no transform"** (see pseudocode).

### 3.4 Big vertices / big triangles

| Type | Field |
|---|---|
| `u32` | numBigVerts |
| `numBigVerts × 16 bytes` | bigVerts — `f32[4]` (Vector4) each |
| `u32` | numBigTris |
| `numBigTris × 12 bytes` | bigTris |

`bhkCMSBigTri`, **12 bytes** (nif.xml `size="12"`; nifly matches exactly):

| Offset | Type | Field |
|---:|---|---|
| 0 | `u16` | v1 |
| 2 | `u16` | v2 |
| 4 | `u16` | v3 |
| 6 | `u32` | material |
| 10 | `u16` | **weldingInfo** |

So **yes — each big triangle carries a welding-info `u16`** (total 12 bytes, not 10 and not 6).
The indices point into **bigVerts** (global for the block), not into any chunk.

### 3.5 Chunks

| Type | Field |
|---|---|
| `u32` | numChunks |
| `numChunks × variable` | chunks |

`bhkCMSChunk` — 24-byte fixed header, then four length-prefixed arrays:

| Offset | Type | Field |
|---:|---|---|
| 0 | `f32[4]` | translation (Vector4) — chunk origin |
| 16 | `u32` | materialIndex (into §3.2 materials) |
| 20 | `u16` | reference (another chunk index; `0xFFFF` normally) |
| 22 | `u16` | transformIndex (into §3.3 transforms) |
| 24 | `u32` | **numVertices — a count of `u16` components, i.e. 3 × vertexCount** |
| 28 | `u16[numVertices]` | vertices (`vertexCount = numVertices / 3`, 6 bytes each: x, y, z) |
| — | `u32` | numIndices |
| — | `u16[numIndices]` | indices (into *this chunk's* vertices) |
| — | `u32` | numStrips |
| — | `u16[numStrips]` | strips (run lengths) |
| — | `u32` | numWeldingInfo |
| — | `u16[numWeldingInfo]` | weldingInfo (a `bhkWeldInfo` bitfield per triangle; usually `numWeldingInfo == numIndices`) |

`bhkWeldInfo` is a `u16` bitfield: bits 0–4 angle edge 1, 5–9 angle edge 2, 10–14 angle edge 3,
bit 15 unused (nif.xml `bitfield name="bhkWeldInfo" storage="ushort"`). You can ignore it for
geometry.

### 3.6 Trailer

| Type | Field |
|---|---|
| `u32` | numConvexPieceA — **last field of the block** |

Nothing follows it. There are **no** shape keys, no convex-piece array, no "transform info" array
after the chunks; the only transform table is §3.3, which sits *before* big verts. (`numConvexPieceA`
counts `hkpCompressedMeshShape::ConvexPiece`, unused by Bethesda, normally 0.)

### 3.7 `bhkCompressedMeshShape` — the shape that points at this data

Block layout (nif.xml ≡ nifly, `versions="#SKY_AND_LATER#"`):

| Type | Field |
|---|---|
| `i32` | Target (block ref to `NiAVObject`, −1 = none) |
| `u32` | userData |
| `f32` | radius (`0.005`) |
| `f32` | unknown float 1 |
| `f32[4]` | scale (`1,1,1,0`) |
| `f32` | radius copy (`0.005`) |
| `f32[4]` | scale copy (`1,1,1,0`) |
| `i32` | Data (block ref to `bhkCompressedMeshShapeData`) |

Facts you need:

* **There is no transform field on the shape.** The object-space placement comes from the block
  chain: `bhkCollisionObject` → `bhkRigidBody` (`Translation`, `Rotation`) → shape. NifSkope's CMS
  renderer does **not** apply the `bhkRigidBody` transform either, so the decoded coordinates below
  are already in the space the game treats as the shape's own — apply the rigid-body transform
  afterwards only if you need the node's parent space.
* **There is no welding type on the shape.** It is the `u8` at offset 52 of the *data* block (§3.1).
* `scale` / `scale copy` are `(1,1,1,0)` in vanilla data and NifSkope does not apply them. Do not
  multiply vertices by them unless you have confirmed non-unit values in the file you are parsing;
  if you must, apply to xyz only.

### 3.8 Combining chunk translation and quantised vertices

The dequantisation is a fixed **1/1000** scale applied to an **unsigned** `u16` triplet — so each
component spans `0.000 … 65.535` units relative to the chunk origin (not ±32.7). NifSkope `drawCMS`:

```
vertex = chunkOrigin + chunkTranslation + (qx/1000, qy/1000, qz/1000, 0)   // Vector4 math
vertex = chunkRotation * vertex                                           // rotate the result
```

* `chunkOrigin` = `chunk.translation` (offset 0 of the chunk record).
* `chunkTranslation` / `chunkRotation` = `transforms[chunk.transformIndex].translation/.rotation`.
* The rotation is applied to the *final* position (i.e. the whole chunk is rotated about the shape
  origin), which is what the working renderer does; in practice chunk transforms are identity, so
  this reduces to `vertex = chunk.translation + quantised/1000`.
* Chunks with `transformIndex >= numTransforms` (and `0xFFFF` sentinels) must be treated as
  identity — NifSkope would index out of bounds here, so add the guard.

### 3.9 Resolving triangle indices

Two independent index spaces, never mixed:

* **Big triangle** (`§3.4`): `v1, v2, v3` index into **bigVerts** (block-global). Use the coordinates
  verbatim — no chunk translation, no /1000, no transform.
* **Small triangles** (chunks): `chunk.indices[]` index into **that chunk's own** `vertices` array
  (i.e. `u16` triplet number `i` is at byte `28 + 6*i` of the chunk record, and its dequantised
  value is `chunk.translation + triplet/1000`). Indices do **not** carry across chunks.

Chunk triangle extraction (`drawCMS`), and the winding caveat:

```
offset = 0
for L in strips:                      # L = run length in indices
    for i in range(L - 2):            # sliding window = triangle strip
        emit(indices[offset+i], indices[offset+i+1], indices[offset+i+2])
    offset += L
for f in range(0, numIndices - offset, 3):    # leftover = plain triangle list
    emit(indices[offset+f], indices[offset+f+1], indices[offset+f+2])
```

`sum(strips)` must be `<= numIndices`. The sliding-window part is a triangle *strip*: NifSkope does
**not** alternate the winding, so if you need consistent outward normals flip `(v2, v3)` on odd `i`
(standard D3D strip convention). For collision use (double-sided) it is normally irrelevant.

---

## 4. Pseudocode — one `bhkCompressedMeshShapeData` → triangles in object space

```python
def read_cms_data(buf, off):                     # buf: bytes of the block payload, off: start
    r = Reader(buf, off)                         # little-endian primitives

    bits_index   = r.u32()
    bits_windex  = r.u32()
    mask_windex  = r.u32()
    mask_index   = r.u32()
    error        = r.f32()
    aabb_min     = r.f32v(4)
    aabb_max     = r.f32v(4)
    welding_type = r.u8()
    material_type= r.u8()

    assert mask_windex == (1 << bits_windex) - 1, "maskWIndex/maskIndex swapped?"
    assert mask_index  == (1 << bits_index)  - 1

    mat32 = r.u32v(r.u32())
    mat16 = r.u32v(r.u32())
    mat8  = r.u32v(r.u32())

    materials = []
    for _ in range(r.u32()):
        materials.append(dict(material=r.u32(), layer=r.u8(), flags=r.u8(), group=r.u16()))

    r.u32()                                       # numNamedMaterials (no array follows)

    transforms = []
    for _ in range(r.u32()):
        t = r.f32v(4)                             # translation
        q = r.f32v(4)                             # quaternion x, y, z, w
        transforms.append((t, q))

    big_verts = [r.f32v(4) for _ in range(r.u32())]

    big_tris = []
    for _ in range(r.u32()):
        v = (r.u16(), r.u16(), r.u16())
        mat = r.u32()
        weld = r.u16()                            # 12 bytes total — do not skip
        big_tris.append((v, mat))

    chunks = []
    for _ in range(r.u32()):
        c = {}
        c['translation'] = r.f32v(4)
        c['material']    = r.u32()
        c['reference']   = r.u16()
        c['transform']   = r.u16()
        n = r.u32()                               # count of u16 COMPONENTS
        assert n % 3 == 0
        c['verts'] = [(r.u16(), r.u16(), r.u16()) for _ in range(n // 3)]
        c['indices'] = r.u16v(r.u32())
        c['strips']  = r.u16v(r.u32())
        c['weld']    = r.u16v(r.u32())
        chunks.append(c)

    r.u32()                                       # numConvexPieceA — end of block

    # ---- triangles in object space -------------------------------------------------
    tris = []                                     # (v0, v1, v2, material)

    for (v, mat) in big_tris:                     # big verts are already object space
        p = [big_verts[i][:3] for i in v]
        tris.append((p[0], p[1], p[2], mat))

    for c in chunks:
        t  = c['translation']
        ti = c['transform']
        trans, quat = transforms[ti] if ti < len(transforms) else ((0,0,0,0), (0,0,0,1))
        rot = quat_to_matrix(quat)                # xyzw; identity if quat is (0,0,0,1)

        pts = []
        for (qx, qy, qz) in c['verts']:
            v = (t[0] + trans[0] + qx / 1000.0,
                 t[1] + trans[1] + qy / 1000.0,
                 t[2] + trans[2] + qz / 1000.0)
            pts.append(mat3_mul_vec3(rot, v))     # NifSkope rotates the final position

        mat = materials[c['material']]['material'] if c['material'] < len(materials) else 0
        idx, offset = c['indices'], 0
        for L in c['strips']:                     # triangle strips
            for i in range(L - 2):
                tris.append((pts[idx[offset+i]], pts[idx[offset+i+1]],
                             pts[idx[offset+i+2]], mat))
            offset += L
        for f in range(0, len(idx) - offset, 3):  # leftover plain triangles
            tris.append((pts[idx[offset+f]], pts[idx[offset+f+1]],
                         pts[idx[offset+f+2]], mat))

    return tris
```

Notes for the implementation:

* Every array in this block is `u32 count` + elements. There are no implicit lengths.
* Validate `numVertices % 3 == 0`, `sum(strips) <= numIndices`, `numIndices` even-ish, and every
  index `< len(verts)`; vanilla data passes, corrupt mod data does not.
* Allocate nothing from a count before checking it against `len(buf) - pos` — this is the standard
  crash vector in NIF parsers.
* `aabb_min/max` are a cheap cross-check: `min/max` over your decoded chunk triangles should be
  inside them (they are the bound Havok baked, and they exclude big verts in some files).

---

## 5. Sources

Primary / spec:

* niftools NIF XML specification (`nif.xml`) — `Header` struct, `bhkCompressedMeshShapeData`,
  `bhkCMSChunk`, `bhkCMSBigTri`, `bhkMeshMaterial`, `bhkQsTransform`, `hkAabb`, `HavokFilter`,
  `bhkWeldInfo`, `SizedString`, `ExportString`, `HeaderString`, `#BSSTREAMHEADER#`:
  <https://github.com/niftools/nifxml/blob/develop/nif.xml>
  (raw: <https://raw.githubusercontent.com/niftools/nifxml/develop/nif.xml>)
* niftools/nifxml commit that rewrote `bhkCMSChunk` and introduced `bhkWeldInfo`
  (`Num Vertices` = "Number of vertices, multiplied by 3", `Vertices` = `UshortVector3`,
  "havok coordinates*1000"): <https://github.com/niftools/nifxml/commit/8b727dda>

Working implementations:

* **NifSkope** `drawCMS()` — the actual CMS decode (`chunkOrigin + chunkTranslation + q/1000`,
  rotation, strip walk, big tris): <https://github.com/hexabits/nifskope/blob/develop/src/gl/gltools.cpp>
* **NifSkope** loader — header model, block sizes ignored by default, `Ignore Block Size`,
  position-vs-size check, block type index `& 0x7FFF`:
  <https://github.com/hexabits/nifskope/blob/develop/src/model/nifmodel.cpp>
* **NifSkope** BSA reader (`inflateInit2(&strm, 15 + 32)`): <https://github.com/hexabits/nifskope/blob/develop/lib/fsengine/bsa.cpp>
* **nifly** (`ousnius/nifly`, the library behind PyNifly/BodySlide) —
  `bhkCompressedMeshShapeData::Sync` field order, `bhkCMSDChunk`, `bhkCMSDBigTris`,
  `bhkCMSDMaterial`, `bhkCMSDTransform`: <https://github.com/ousnius/nifly/blob/main/src/bhk.cpp>
  and <https://github.com/ousnius/nifly/blob/main/include/bhk.hpp>
* **nifly** header reader (`NiHeader::Get`) — exact header order, BS header, string encodings,
  groups, footer: <https://github.com/ousnius/nifly/blob/main/src/BasicTypes.cpp>
  and <https://github.com/ousnius/nifly/blob/main/include/BasicTypes.hpp>
* **xEdit / TES5Edit** `wbBSArchive.pas` — BSA v105 compression type (`ctLZ4Frame`), 24-byte SSE
  folder record, size-bit XOR, embedded-name and 4-byte originalSize handling:
  <https://github.com/TES5Edit/TES5Edit/blob/dev-4.1.5/Core/wbBSArchive.pas>
* **bsatk** (`ModOrganizer2/modorganizer-bsatk`) — independent v105 confirmation:
  `TYPE_SKYRIMSE` → `LZ4F_decompress`, `readFolderSE` = 24 bytes with `u64` offset,
  `COMPRESSMASK 0xC0000000` / `SIZEMASK 0x3fffffff`:
  <https://github.com/ModOrganizer2/modorganizer-bsatk/blob/master/src/bsaarchive.cpp>,
  <https://github.com/ModOrganizer2/modorganizer-bsatk/blob/master/src/bsafolder.cpp>,
  <https://github.com/ModOrganizer2/modorganizer-bsatk/blob/master/src/bsafile.cpp>
* **PyNifly** (SSE + FO4 CMS import/export, uses nifly; documents the 255-vert/255-face chunk
  limit): <https://github.com/BadDogSkyrim/PyNifly/wiki/FO4-Collisions>

Secondary / cross-check:

* UESP, Skyrim BSA format (header, flags, v105 folder-record 24-byte description, "30th bit
  (0x40000000)", compressed block layout, LZ4 note for v105):
  <https://en.uesp.net/wiki/Skyrim_Mod:Archive_File_Format>
* NifSkope issue #67 — corroborates that `Num Vertices`/`Vertices` is an array of `u16` components
  (`/3`) and the 6-byte `UshortVector3` size: <https://github.com/hexabits/nifskope/issues/67>
