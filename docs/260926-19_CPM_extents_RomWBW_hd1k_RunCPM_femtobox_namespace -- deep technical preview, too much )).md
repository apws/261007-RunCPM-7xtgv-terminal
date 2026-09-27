# CP/M Extents, RomWBW `hd1k`, RunCPM, and a Virtual Tree Namespace

**Project:** 7xtgv / RunCPM / CP/M tooling  
**Date:** 2026-09-26  
**Status:** Technical reference and design notes

---

## 1. Purpose

This note consolidates the discussion around:

- CP/M 2.2 logical records, allocation blocks, extents, and directory entries;
- the difference between a **logical CP/M extent** and a **physical directory entry**;
- the current RunCPM fake DPB and what remains meaningful when files are really stored in host/FAT directories;
- RomWBW `hd1k` disk geometry and its 8 MiB slice model;
- RomWBW `ASSIGN` and the mapping of physical slices onto CP/M drive letters;
- a proposed `ls` / `ll -wide` presentation using **DExt** rather than only logical extent count;
- a small "femtobox" / custom-CCP namespace layer presenting an apparent Unix-like tree over CP/M drive/user areas without changing the underlying CP/M filesystem;
- the possibility of adding a RomWBW-slice backend to RunCPM.

The main design goal is to remain completely compatible with ordinary CP/M applications at the bottom while making storage layout understandable and pleasant at the shell/file-manager level.

---

# 2. The CP/M storage hierarchy

The useful hierarchy is:

```text
128-byte logical record
        ↓
allocation block
        ↓
logical extent
        ↓
directory entry / physical extent
        ↓
file
```

These layers are related but are not interchangeable.

---

## 2.1 Logical record

The fundamental CP/M BDOS file-I/O unit is:

```text
1 logical record = 128 bytes
```

Sequential BDOS reads and writes advance one 128-byte record at a time.

For a host-backed emulator this remains semantically important even when the host filesystem itself uses completely different sectors/clusters.

Useful derived value:

```text
records = ceil(file_bytes / 128)
```

A record count is mathematically redundant with a byte count, but it is not semantically redundant in CP/M: BDOS, FCBs, and classic CP/M utilities think in 128-byte records.

---

## 2.2 Allocation block

An allocation block is CP/M's filesystem allocation unit.

Typical block sizes are:

```text
1 KiB
2 KiB
4 KiB
8 KiB
16 KiB
```

The block size is described indirectly by the DPB:

```text
BSH = log2(block_size) - 7
BLM = block_size / 128 - 1
```

For a 4 KiB allocation block:

```text
block_size = 4096 B
BSH        = 5
BLM        = 31
records/block = 32
```

Allocation blocks are the objects referenced by CP/M directory entries.

They do not need to be contiguous.

---

# 3. The fixed 16 KiB logical extent

For CP/M 2.2, the useful invariant is:

```text
1 logical extent = 128 records
                 = 128 × 128 bytes
                 = 16384 bytes
                 = 16 KiB
```

Therefore:

```text
logical_extents = ceil(records / 128)
                = ceil(file_bytes / 16384)
```

Historically, utilities such as `STAT` can report an extent count meaning these logical 16 KiB extents.

This logical extent size should be kept conceptually separate from the amount of data represented by one physical 32-byte directory entry.

---

# 4. Directory entries and the extent mask (`EXM`)

A CP/M directory entry is 32 bytes.

Among other things, it contains:

- user number;
- filename;
- extension;
- extent-related fields (`EX`, `S1`, `S2`);
- record count (`RC`);
- allocation-block numbers.

Large files are represented by multiple directory entries with the same filename.

However, **one directory entry does not always represent only one 16 KiB logical extent**.

The DPB field `EXM` controls this relationship.

A useful relationship is:

```text
logical extents represented by one directory entry = EXM + 1
```

Thus:

```text
EXM = 0  -> 1 logical extent  / directory entry
EXM = 1  -> 2 logical extents / directory entry
EXM = 3  -> 4 logical extents / directory entry
EXM = 7  -> 8 logical extents / directory entry
```

With `EXM=1`:

```text
1 directory entry
= 2 logical extents
= 2 × 16 KiB
= 32 KiB maximum file-data span
```

---

# 5. Project terminology: `DExt`

For our tools it is useful to distinguish the historic **logical extent** from the amount of a file represented by one physical directory slot.

This document uses:

```text
EXT   = CP/M logical extent = 16 KiB

DExt  = directory-entry extent
      = one 32-byte CP/M directory entry worth of file mapping
```

`DExt` is a **project/UI term**, not a claim that this is a standard CP/M command-line abbreviation.

For a DPB with `EXM=1`:

```text
1 DExt = 2 EXT
        = 32 KiB maximum represented file data
        = 1 physical directory entry
```

Therefore:

```text
DExt(file) = ceil(logical_extents / (EXM + 1))
```

and with `EXM=1`:

```text
DExt(file) = ceil(file_bytes / 32768)
```

subject to the usual CP/M record/file-size semantics for the final partial entry.

This number is particularly interesting because on a real CP/M disk it is also the number of finite physical directory slots consumed by the file.

---

# 6. Why `DExt` is attractive for `ls` / `ll -wide`

For the current project, both:

1. current RunCPM's fake CP/M geometry; and
2. RomWBW modern `hd1k`;

use:

```text
allocation block = 4 KiB
EXM              = 1
```

Therefore both have the same convenient mental relationship:

```text
1 EXT  = 16 KiB logical
1 DExt = 32 KiB directory-entry span
```

This makes `DExt` a useful shared diagnostic/display number.

A possible wide listing:

```text
NAME.EXT          BYTES      KB    RECS   DEXT
WORDSTAR.COM      18432     18.0    144      1
BIGFILE.DAT       74368     72.6    581      3
FOO.TXT            1037      1.1      9      1
```

Possible formulas for the shared `EXM=1` case:

```text
RECS = ceil(bytes / 128)
EXT  = ceil(bytes / 16384)
DEXT = ceil(bytes / 32768)
```

`EXT` may still be optionally shown in a very technical mode, but `DExt` has a direct relationship to **directory-resource consumption** on a real CP/M filesystem.

Recommended ordinary wide display:

```text
NAME.EXT        SIZE        RECS   DEXT
```

Optional deep CP/M display:

```text
NAME.EXT        BYTES   RECS   EXT   DEXT   BLOCKS
```

where:

```text
BLOCKS = ceil(bytes / allocation_block_size)
```

For RomWBW `hd1k` and RunCPM's fake DPB:

```text
BLOCKS = ceil(bytes / 4096)
```

---

# 7. Current RunCPM storage model

RunCPM intentionally does **not** emulate a physical CP/M disk image.

Its host layout represents drives and user areas using directories, approximately:

```text
A/0/
A/1/
...
A/9/
A/A/
...
A/F/

B/0/
...
P/F/
```

Current RunCPM requires user-area directories and supports the classic CP/M limits:

```text
16 drives     A: ... P:
16 user areas 0 ... 15
```

Thus the conceptual maximum namespace is:

```text
16 × 16 = 256 flat CP/M drive/user namespaces
```

The actual file bytes reside in normal host files.

On an MCU build this may ultimately be FAT32/SdFat storage.

Therefore actual physical storage allocation is handled by the host filesystem, not by a CP/M allocation bitmap.

---

# 8. RunCPM's fake DPB

RunCPM still must present enough CP/M filesystem semantics for programs using BDOS/FCB structures.

The current upstream source constructs a fake CP/M DPB with:

```text
SPT = 256 logical 128-byte sectors/records per track
BSH = 5
BLM = 31
EXM = 1
DSM = 2039
DRM = 1023
AL0 = FFh
AL1 = 00h
CKS = 0
OFF = 1
```

Derived:

```text
allocation block size = 4096 bytes
allocation blocks     = DSM + 1 = 2040
directory entries     = DRM + 1 = 1024
logical extents/DExt  = EXM + 1 = 2
DExt span             = 32 KiB
```

The source explicitly describes this as a:

```text
CP/M (fake) Disk Parameter Block
```

and computes:

```text
extentsPerDirEntry = extentMask + 1
physicalExtentBytes = logicalExtentBytes * (extentMask + 1)
```

It also constructs a **"phoney allocation block list"** for directory-search compatibility.

This makes the conceptual boundary very clear:

```text
CP/M program / FCB / BDOS view
    records
    extents
    fake allocation blocks
    fake DPB
          ↓
RunCPM host-file implementation
          ↓
normal host files/directories
          ↓
FAT32 / POSIX filesystem / etc.
```

The CP/M geometry is compatibility metadata; it is not the physical on-media filesystem geometry.

---

# 9. What remains meaningful in RunCPM?

The following remain genuinely meaningful CP/M API concepts:

```text
128-byte logical records
FCBs
current record
random record number
drive/user
extent-related FCB state
file size in records
```

The following are mainly compatibility fiction in normal host-directory RunCPM:

```text
physical CP/M disk tracks
physical CP/M sectors
actual CP/M allocation bitmap
actual CP/M allocation blocks
real finite on-disk CP/M directory slots
```

Thus `DExt` in RunCPM should be understood as:

> "the number of CP/M directory-entry spans this file would occupy according to the fake DPB presented to software"

rather than actual host-FAT directory consumption.

Despite that, it is still useful because it uses the same 32 KiB scale as RomWBW `hd1k`.

---

# 10. RomWBW modern `hd1k`

RomWBW provides a contrasting case: the CP/M filesystem is real.

Modern `hd1k` format geometry is documented as:

```text
slice size             = 8,388,608 bytes = 8 MiB
physical sector        = 512 bytes
sectors/track          = 16
tracks                  = 1024
allocation block       = 4096 bytes
directory entries      = 1024
reserved boot tracks   = 2
reserved boot area     = 16,384 bytes
```

Check:

```text
1024 tracks × 16 sectors × 512 bytes
= 8,388,608 bytes
```

Directory start:

```text
2 tracks × 16 sectors × 512 bytes
= 16,384 bytes
```

The 1024 directory entries occupy:

```text
1024 × 32 bytes = 32,768 bytes = 32 KiB
```

At 4 KiB per allocation block:

```text
directory allocation = 8 allocation blocks
```

---

# 11. Derived CP/M 2.2 DPB for `hd1k`

Using standard CP/M DPB relationships and the published `hd1k` geometry:

## SPT

One track contains:

```text
16 × 512 = 8192 bytes
```

In CP/M logical 128-byte records:

```text
SPT = 8192 / 128 = 64
```

So:

```text
SPT = 64
```

## BSH / BLM

For 4096-byte blocks:

```text
BSH = 5
BLM = 31
```

## EXM

For a large disk (`DSM >= 256`) and 4096-byte blocks:

```text
EXM = block_size / 2048 - 1
    = 4096 / 2048 - 1
    = 1
```

Therefore:

```text
EXM = 1
```

and:

```text
2 logical 16 KiB extents / directory entry
1 DExt = 32 KiB
```

## OFF

The format reserves two tracks before the directory:

```text
OFF = 2
```

## DSM

Total tracks:

```text
1024
```

Reserved tracks:

```text
2
```

Filesystem tracks described after `OFF`:

```text
1022
```

Bytes:

```text
1022 × 8192 = 8,372,224 bytes
```

In 4096-byte allocation blocks:

```text
8,372,224 / 4096 = 2044 blocks
```

Therefore:

```text
DSM = 2044 - 1 = 2043
```

## DRM

For 1024 directory entries:

```text
DRM = 1024 - 1 = 1023
```

## AL0 / AL1

Directory storage is:

```text
32 KiB / 4 KiB = 8 allocation blocks
```

The first eight allocation blocks must therefore be reserved for the directory.

This corresponds to:

```text
AL0 = FFh
AL1 = 00h
```

## CKS

For fixed-disk media, directory check-vector size is normally zero:

```text
CKS = 0
```

Thus the useful CP/M 2.2 DPB representation is:

```text
SPT = 64
BSH = 5
BLM = 31
EXM = 1
DSM = 2043
DRM = 1023
AL0 = FFh
AL1 = 00h
CKS = 0
OFF = 2
```

This is a derivation from the published `hd1k` geometry plus standard CP/M 2.2 DPB formulas.

---

# 12. RunCPM vs RomWBW `hd1k`

The particularly useful commonality is:

| Property | RunCPM fake DPB | RomWBW `hd1k` |
|---|---:|---:|
| logical record | 128 B | 128 B |
| allocation block | 4096 B | 4096 B |
| BSH | 5 | 5 |
| BLM | 31 | 31 |
| EXM | 1 | 1 |
| logical extent | 16 KiB | 16 KiB |
| logical extents / directory entry | 2 | 2 |
| **DExt span** | **32 KiB** | **32 KiB** |
| directory entries presented | 1024 | 1024 |
| directory entries physically finite | no, host-backed | yes |
| actual CP/M allocation blocks | synthesized | real |
| underlying storage | host/FAT files | real CP/M filesystem |

The DPBs differ in geometry details such as `SPT`, `DSM`, and `OFF`, but the file-size/directory-entry mental model is strikingly similar.

Therefore the same `ll -wide` columns can be meaningful on both systems:

```text
SIZE    RECS    DEXT
```

with an important semantic distinction:

- RunCPM: `DExt` is a CP/M-compatible synthetic measure;
- RomWBW: `DExt` is real finite CP/M directory-slot consumption.

---

# 13. RomWBW slices

RomWBW uses fixed 8 MiB CP/M filesystems called **slices**.

Conceptually:

```text
physical SD / CF / IDE device

slice 0   8 MiB CP/M filesystem
slice 1   8 MiB CP/M filesystem
slice 2   8 MiB CP/M filesystem
...
slice 255 8 MiB CP/M filesystem
```

Up to 256 slices can be addressed on one large medium:

```text
256 × 8 MiB = 2 GiB
```

A slice is not conceptually the same as an MS-DOS-style dynamically allocated partition.

RomWBW treats slices as fixed-position chunks.

The user is responsible for knowing which slices are initialized/used.

---

# 14. `ASSIGN`

CP/M provides only 16 drive letters:

```text
A: ... P:
```

RomWBW's `ASSIGN` maps those logical drive letters onto physical device/slice pairs.

Conceptually:

```text
C: -> HDSK0:0
D: -> HDSK0:1
E: -> HDSK0:7
F: -> another device/slice
```

Changing the assignment changes what physical 8 MiB filesystem BDOS sees under a drive letter.

This is a storage mapping operation, not a copy.

Layering:

```text
CP/M application
      ↓
BDOS
      ↓
CBIOS
      ↓
logical drive C:
      ↓
RomWBW ASSIGN
      ↓
HDSK0:slice
      ↓
8 MiB real CP/M filesystem
      ↓
HBIOS storage driver
      ↓
SD / CF / IDE
```

With all 16 CP/M drives assigned to `hd1k` slices:

```text
16 × 8 MiB ≈ 128 MiB
```

can be simultaneously visible through CP/M drive letters.

---

# 15. CP/M user areas

Each CP/M drive supports user numbers:

```text
0 ... 15
```

A drive/user pair acts approximately like a flat namespace:

```text
A:0
A:1
...
A:15
B:0
...
P:15
```

Thus the classic drive/user address space provides:

```text
16 drives × 16 users = 256 flat namespaces
```

For a full RomWBW setup with 16 assigned 8 MiB slices, this means:

```text
16 real 8 MiB filesystems
×
16 user namespaces each
```

The capacity remains approximately 128 MiB total visible storage; user areas partition the namespace, not the underlying block capacity.

---

# 16. Proposed apparent tree / "femtobox" namespace

The goal is **not** to implement a new hierarchical filesystem inside CP/M.

Instead, provide a shell/file-manager view which maps human-readable paths onto ordinary drive/user pairs.

Example:

```text
/
├── bin
├── apps
│   ├── editors
│   ├── languages
│   └── utils
├── src
│   ├── 7xtgv
│   └── tests
├── docs
├── games
└── home
```

Possible mapping:

```text
/               -> A:0
/bin            -> A:1
/apps/editors   -> B:1
/apps/languages -> B:2
/apps/utils     -> B:3
/src            -> C:0
/src/7xtgv      -> C:1
/src/tests      -> C:2
/docs           -> D:0
/games          -> E:0
/home           -> F:0
```

At the bottom nothing changes.

An ordinary application still sees only:

```text
current drive
current user area
flat 8.3 filenames
```

---

# 17. Intermediate tree nodes need not consume user areas

The apparent tree does not have to map every node.

For example:

```text
/apps/editors/wordstar
```

may resolve directly to:

```text
H:6
```

without `/apps` or `/apps/editors` themselves corresponding to physical drive/user namespaces.

This makes arbitrary apparent depth possible while preserving the finite pool of 256 drive/user pairs.

The tree is a namespace database, not physical directory nesting.

---

# 18. Path resolution

A pathname should remain a shell-level object until resolution.

Example:

```text
/src/7xtgv/TERM.C
```

resolution:

```text
/src/7xtgv -> C:1

result:
    drive = C
    user  = 1
    file  = TERM.C
```

Only after path resolution should a CP/M FCB be constructed.

This prevents path syntax from leaking into the ordinary CP/M filename abstraction.

A minimal conceptual structure:

```c
struct namespace_target {
    uint8_t drive;
    uint8_t user;
};

struct namespace_mount {
    const char *path;
    struct namespace_target target;
};
```

The actual implementation can be significantly more compact on an 8-bit target.

---

# 19. `cd` semantics

`cd /src/7xtgv` can be implemented approximately as:

```text
resolve "/src/7xtgv"
        ↓
C:1
        ↓
BDOS select drive C:
BDOS select user 1
        ↓
shell updates virtual PWD
```

An ordinary CP/M program launched afterward needs no awareness of the virtual tree.

Example:

```text
cd /apps/editors
zde
```

The shell selects the mapped drive/user first.

`ZDE.COM` then runs in a normal flat CP/M directory.

This is one of the most valuable properties of the design: compatibility requires no modifications to legacy applications.

---

# 20. Curated mapping rather than real `mkdir`

The initial implementation does not need automatic:

```text
mkdir
rmdir
```

The apparent hierarchy can simply be a curated configuration describing where things live.

Example:

```text
/               = A:0
/bin            = A:1
/apps/editors   = B:1
/apps/languages = B:2
/src/7xtgv      = C:1
/docs           = D:0
```

This may actually be preferable:

- no hidden movement of CP/M files;
- no allocator for drive/user namespaces;
- no confusing fake filesystem semantics;
- trivial implementation;
- the tree remains a human-readable index of storage organization.

Dynamic directory creation can be considered later if it proves genuinely useful.

---

# 21. Aliases are possible

Because this is a namespace rather than an on-disk tree, multiple paths may point at the same drive/user pair.

Example:

```text
/bin        -> A:1
/apps/utils -> A:1
```

This can be useful for classification/navigation without duplicating files.

The apparent tree is therefore closer to a set of named namespace mounts than to a strict inode directory tree.

---

# 22. Two-panel file manager

A two-panel file manager is a particularly natural UI for this model.

Example:

```text
LEFT                         RIGHT
/src/7xtgv                   /apps/editors

..                           ..
ATTR.C                       ZDE.COM
RENDER.C                     WS.COM
TERM.C                       VDE.COM
...
```

Each pane needs to retain only something conceptually like:

```text
virtual pathname
resolved drive
resolved user
```

Cross-panel copy then becomes an ordinary CP/M file copy between two drive/user contexts.

The human navigates the apparent tree.

The file operation layer deals only with ordinary CP/M namespaces.

---

# 23. Two layers of mounting with RomWBW

RomWBW gives an especially elegant arrangement because two independent mappings exist:

## Layer 1: RomWBW `ASSIGN`

```text
CP/M drive -> physical device:slice
```

Example:

```text
C: -> HDSK0:3
D: -> HDSK0:17
```

## Layer 2: femtobox namespace

```text
virtual path -> CP/M drive:user
```

Example:

```text
/src/7xtgv -> C:1
/docs      -> D:4
```

Thus:

```text
human pathname
      ↓
virtual namespace map
      ↓
drive:user
      ↓
RomWBW ASSIGN
      ↓
device:slice
      ↓
physical CP/M filesystem
```

This cleanly separates:

- human organization;
- CP/M logical namespace;
- physical slice placement.

A slice can be reassigned without changing the apparent high-level tree if the drive mapping is restored appropriately.

---

# 24. Same namespace model on RunCPM

RunCPM already has a direct host representation of drive/user pairs.

For example:

```text
C:1
```

maps approximately to:

```text
C/1/
```

in the host storage tree.

Therefore the same virtual pathname resolver can be used on RunCPM:

```text
/src/7xtgv
      ↓
C:1
      ↓
host C/1/
```

This means the namespace concept should preferably **not** be designed as a RunCPM-only feature.

The common abstraction is:

```text
drive:user
```

with different backends underneath.

---

# 25. Possible tiny VFS architecture

A minimal architecture could be:

```text
             femtobox namespace
                    │
               path resolver
                    │
                mount table
                    │
             drive:user target
                    │
          ┌─────────┼─────────┐
          │         │         │
       RunCPM     BDOS     raw-image
       hostdir   backend     backend
```

No modern VFS complexity is initially required.

Specifically, it does not need:

- Unix inodes;
- symlinks;
- permissions;
- arbitrary filenames;
- real nested directories;
- journaling;
- hard links;
- elaborate metadata.

The central object can remain essentially:

```text
virtual path
      ↕
drive:user
```

---

# 26. Possible femtobox command scope

A very small shell/tool set would already provide a large usability improvement:

```text
pwd
cd
ls
ll
cp
mv
rm
cat / type
mount / map
```

Optional later utilities:

```text
df
du
find
tree
mkdir     (virtual namespace only)
rmdir     (virtual namespace only)
```

The philosophy should be:

> improve navigation and handling without pretending the underlying CP/M filesystem has become Unix.

---

# 27. Recommended `ls` / `ll` semantics

## Compact listing

Traditional CP/M-style or shell-style filename list.

## Long listing

Suggested:

```text
NAME.EXT          SIZE       RECS   DEXT
```

Possible example:

```text
ZDE.COM          26.5K        212      1
WORDSTAR.COM     48.0K        384      2
MANUAL.TXT      103.2K        826      4
```

For `EXM=1` systems:

```text
DExt = ceil(file_bytes / 32768)
```

This works especially naturally across:

- current RunCPM fake DPB;
- RomWBW `hd1k`.

## Deep CP/M diagnostic mode

Possible:

```text
NAME.EXT     BYTES     RECS   EXT   DEXT   BLOCKS
```

Definitions:

```text
RECS   = ceil(bytes / 128)
EXT    = ceil(bytes / 16384)
DEXT   = ceil(EXT / (EXM + 1))
BLOCKS = ceil(bytes / block_size)
```

The displayed values should be derived from the active DPB where possible rather than hard-coded globally.

---

# 28. Why show `DExt` instead of only `EXT`?

`EXT` is historically meaningful, but for our current common geometry:

```text
EXT = one 16 KiB logical unit
```

whereas:

```text
DExt = one physical 32-byte directory entry
     = one finite directory resource on real CP/M
     = 32 KiB file span for EXM=1
```

RomWBW `hd1k` has:

```text
1024 directory entries total
```

Therefore `DExt` directly expresses consumption of a genuinely finite metadata resource.

A file requiring:

```text
DExt = 20
```

uses 20 of the slice's 1024 physical directory slots.

That is more operationally interesting than merely saying it has 40 logical extents.

On RunCPM the physical limit is not real, but the same value preserves a common mental model and reflects the geometry RunCPM presents to CP/M software.

---

# 29. Important terminology caveat

Avoid saying:

```text
"an extent is always 32 KiB"
```

That is wrong.

Correct:

```text
CP/M logical extent = 16 KiB
```

For the specific `EXM=1` geometries used by current RunCPM and RomWBW `hd1k`:

```text
one 32-byte directory entry represents up to:
2 logical extents = 32 KiB
```

Our `DExt` column deliberately means the latter.

---

# 30. RomWBW image / slice backend for RunCPM

It should be feasible to add a RunCPM backend which directly accesses a real RomWBW `hd1k` slice instead of mapping a CP/M drive to a host directory.

Conceptually:

```text
RunCPM BDOS/file layer
        ↓
CP/M filesystem backend
        ↓
RomWBW image/slice
```

For a read-only implementation, the necessary operations are fairly small:

1. calculate slice base;
2. know the DPB/geometry;
3. locate and scan the CP/M directory;
4. match user number and 8.3 filename;
5. group directory entries belonging to the same file;
6. order the extent information correctly;
7. follow allocation-block numbers;
8. expose the result as 128-byte logical records.

For `hd1k`, important known geometry is:

```text
slice bytes       = 8,388,608
boot area         = 16,384
directory entries = 1024
allocation block  = 4096
EXM               = 1
```

---

# 31. Writing a raw RomWBW slice

Write support is more involved but still small compared with a modern filesystem.

It requires:

- reconstructing used/free allocation blocks;
- finding free directory entries;
- allocating blocks;
- creating/updating extent directory entries;
- maintaining `EX`, `S2`, `RC` correctly;
- deleting/releasing allocation;
- rename handling;
- safe behavior on incomplete writes.

Such a backend could eventually allow the same media image to be used by:

- real RomWBW hardware;
- a RomWBW emulator;
- RunCPM / 7xtgv development tooling.

That could be useful for testing and file exchange.

---

# 32. Suggested backend abstraction

Possible high-level structure:

```text
backend: HOSTDIR
    drive:user -> host directory

backend: BDOS
    drive:user -> currently running CP/M filesystem

backend: CPM_IMAGE
    drive:user -> CP/M filesystem inside raw image/slice
```

The apparent virtual tree does not need to know which backend supplies the final files.

---

# 33. RomWBW combo-image note

Modern RomWBW tooling also uses combo images with:

```text
1 MiB prefix
+ N × 8 MiB hd1k slices
```

A documented slice address formula is:

```text
slice_base = 1,048,576 + (8,388,608 × slice_number)
```

for that combo-image layout.

A plain single `hd1k` image is simply one 8 MiB slice beginning at byte zero.

A real physical RomWBW medium's slice selection should follow RomWBW/HBIOS conventions rather than assuming every file/image necessarily has the combo-file prefix.

---

# 34. Historical vs project-facing numbers

For humans working on this project, the most useful storage scale ladder is probably:

```text
128 B    = CP/M logical record
4 KiB    = allocation block          (RunCPM fake DPB + RomWBW hd1k)
16 KiB   = logical extent
32 KiB   = DExt / directory entry    (because EXM=1)
8 MiB    = one RomWBW hd1k slice
128 MiB  = 16 simultaneously mapped 8 MiB slices
2 GiB    = 256 possible 8 MiB slices on one RomWBW large device
```

And namespace scale:

```text
16 user areas / CP/M drive
16 drive letters
256 drive:user namespaces concurrently addressable
```

This is a compact and useful mental model.

---

# 35. Design recommendation

For 7xtgv / RunCPM shell tools:

## Use `DExt` in wide listings

Preferred:

```text
NAME.EXT        SIZE      RECS   DEXT
```

Reasons:

- `SIZE` is human-friendly;
- `RECS` exposes the fundamental CP/M BDOS I/O unit;
- `DExt` exposes the directory-entry scale;
- the same 32 KiB `DExt` scale applies to current RunCPM and RomWBW `hd1k`;
- on real RomWBW it expresses actual finite directory-slot consumption.

Optionally expose `EXT` and `BLOCKS` in a diagnostic mode.

## Keep path hierarchy above CP/M

Do not encode paths in CP/M filenames.

Resolve:

```text
virtual pathname -> drive:user + filename
```

before creating/using an FCB.

## Keep the first namespace implementation curated

Start with a static/user-editable mapping table.

No requirement for automatic `mkdir`/`rmdir`.

## Keep legacy applications untouched

Before launching an application:

```text
select mapped drive
select mapped user
run ordinary CP/M program
```

The application sees only the normal flat CP/M directory.

## Consider the namespace independent of RunCPM

The same apparent tree can work on:

- host-directory RunCPM;
- real RomWBW CP/M;
- a future raw-CP/M-image backend.

---

# 36. Example end-to-end view

Human view:

```text
/src/7xtgv/TERM.C
```

Namespace map:

```text
/src/7xtgv -> C:1
```

CP/M view:

```text
drive C:
user  1
file  TERM.C
```

RomWBW physical mapping:

```text
C: -> HDSK0:17
```

Actual location:

```text
HDSK0 slice 17
user area 1
TERM.C
```

For RunCPM instead:

```text
C:1 -> host C/1/TERM.C
```

The shell/file manager therefore provides one consistent human-facing namespace while preserving each backend's native storage model.

---

# 37. Sources / verification notes

Primary/current references consulted for this note:

1. **RomWBW upstream repository / documentation**  
   https://github.com/wwarthen/RomWBW

2. **RomWBW emulator disk-format documentation**  
   `docs/DISK_FORMATS.md` in `avwohl/romwbw_emu`  
   https://github.com/avwohl/romwbw_emu/blob/main/docs/DISK_FORMATS.md

   Relevant documented `hd1k` facts:
   - 8,388,608-byte slices;
   - 512-byte sectors;
   - 16 sectors/track;
   - 1024 tracks;
   - 4096-byte allocation blocks;
   - 1024 directory entries;
   - two reserved boot tracks;
   - directory begins at byte 16,384.

3. **RunCPM upstream**  
   https://github.com/MockbaTheBorg/RunCPM

   Current `RunCPM/cpm.h` constructs the fake DPB:
   - SPT 256;
   - BSH 5;
   - BLM 31;
   - EXM 1;
   - DSM 2039;
   - DRM 1023;
   - AL0 FFh;
   - AL1 00h;
   - CKS 0;
   - OFF 1.

   It explicitly calls this the CP/M **fake Disk Parameter Block**, computes
   `extentsPerDirEntry = extentMask + 1`, and constructs a "phoney allocation block list".

4. Standard CP/M 2.2 DPB relationships:
   - logical file record = 128 bytes;
   - `BSH = log2(block_size) - 7`;
   - `BLM = block_size / 128 - 1`;
   - for `DSM >= 256`, `EXM = block_size / 2048 - 1`.

The exact RomWBW `hd1k` DPB values in section 11 are derived from the published modern `hd1k` geometry using these standard CP/M 2.2 DPB formulas.

---

# 38. Short reference card

```text
CP/M:
    record       128 B
    logical EXT  16 KiB

RunCPM fake DPB:
    block        4 KiB
    EXM          1
    DExt         32 KiB
    directory    1024 synthetic entries
    storage      host files

RomWBW hd1k:
    block        4 KiB
    EXM          1
    DExt         32 KiB
    directory    1024 real entries
    slice        8 MiB
    boot area    16 KiB

RomWBW:
    16 users / drive
    16 drive letters simultaneously
    256 drive:user namespaces
    up to ~128 MiB simultaneously mapped
    up to 256 × 8 MiB slices / large device

Suggested ll -wide:
    NAME.EXT   SIZE   RECS   DEXT

Project virtual tree:
    /path -> drive:user

Legacy app:
    still runs in one ordinary flat CP/M drive/user directory
```

---

## 39. Working conclusion

The useful common geometry between current RunCPM and RomWBW `hd1k` is unusually convenient:

```text
4 KiB allocation blocks
EXM = 1
2 × 16 KiB logical extents per directory entry
= 32 KiB per DExt
```

This makes `DExt` a good project-facing measure for `ls` / `ll -wide`.

More broadly, RomWBW's slice/`ASSIGN` model and CP/M's drive/user namespace provide a natural substrate for a very small apparent hierarchical tree:

```text
human tree
    ↓
drive:user
    ↓
ordinary CP/M filesystem/application semantics
```

The design improves navigation and organization without attempting to redesign CP/M itself.
