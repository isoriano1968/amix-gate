# Page-cross ring: decoding `z3660_pagecross_ring` by hand

`z3660_pagecross_n` counts the chunks whose one physical range crosses a page edge in
`z3660_rw()`, i.e. `(pa & (NBPP-1)) + len > NBPP`. NBPP is 4096 on the 040/060 line and 2048 on
the 030 line. The count says how many chunks crossed. The ring records which ones: each
counted crossing is also copied into a 16-entry ring that can be read off a running kernel
with kmemdump, adb or READ_MEM.

**Recording only.** The transfer that follows the census is unchanged. The ring is written and
never read by the driver. The ZS-04 guard (`blocks > nbyte/bs` → EIO, no doorbell) runs
earlier, in `z3660queue()`, so a refused request never reaches the census or the ring.

**Which kernels carry it.** Driver objects built from this repo's source at or after the commit
that added the ring. The shipping KD060-Z object predates it and has no ring. kerntools cuts a
`-CD` sibling that carries it, for the diagnostic boot only.

## Symbols

| symbol | kind | size | meaning |
|---|---|---|---|
| `z3660_pagecross_ring` | `D` (initialized `.data`) | 516 B | magic + 16 entries |
| `z3660_pagecross_ring_idx` | `C` (COMMON → `.bss`) | 4 B | slot the **next** crossing takes, 0..15 |
| `z3660_pagecross_n` | `C` (COMMON → `.bss`) | 4 B | total crossings this boot (unchanged semantics) |

The two COMMON symbols get their addresses only at the final link. Take them from kerntools'
**post-COMMON** map, as for the other counters (DI-1-misplaced-write.md §5). Runtime address =
load base + map offset. On the KD line the base is 0x08000000.

## Layout (32-bit big-endian longs, no padding)

The driver asserts the size at compile time for the kernel: `sizeof` must be 4 + 16 × 32.

```
ring + 0x000   magic   0x5A504352  "ZPCR"
ring + 0x004   entry 0
ring + 0x024   entry 1
   ...         entry i at ring + 4 + 32*i
ring + 0x1E4   entry 15      (end at ring + 0x204 = 516 B)

entry + 0   seq     value of z3660_pagecross_n just after this crossing (1, 2, ...);
                    0 = the slot has never been written
entry + 4   flags   bit 0: 1 = WRITE (to device), 0 = READ
entry + 8   unit    piscsi unit
entry +12   block   LBA of THIS chunk, in the unit's blocks
entry +16   len     bytes in this chunk
entry +20   pa      physical start of this chunk (what the firmware got as *_ADDR3)
entry +24   req     cp->addr: physical start of the whole request
entry +28   reqlen  bytes in the whole request (blocks * bs)
```

`req` is the closest thing to "the caller's buffer" that the driver can see. `startio()`
`vtop()`s `b_addr` before `z3660queue()` runs, and the driver never translates addresses, so
the caller's kernel VA is not available. `pa - req` is the chunk's byte offset inside the
request. It is nonzero only for the second and later 64 KiB chunks of one request.

`seq` is written last, so a slot with a nonzero `seq` is complete. Writers run inside the
driver's `spl6` mailbox bracket.

## Decode recipe

1. Find `z3660_pagecross_ring`, `z3660_pagecross_ring_idx` and `z3660_pagecross_n` in the
   kernel's post-COMMON map. Add the load base to each offset.
2. Read 516 bytes at the ring address, plus 4 bytes at each of the other two. Use an
   lseek+read tool such as `zz9k_memprobe -q`, READ_MEM or adb. **Never `dd skip=`**: on a
   char device it reads its way to the offset.
3. **Base check:** the first 4 bytes must be `5A 50 43 52` ("ZPCR"). Anything else means the
   address is wrong. Check the base and the map before reading further.
4. Split the rest into 16 × 32-byte entries and each entry into 8 big-endian longs. Drop the
   entries with `seq == 0`. Sort the rest by `seq`. The newest is at slot `(idx - 1) & 15`.
   If `z3660_pagecross_n > 16`, the ring has wrapped: only seq `n-15 .. n` survive, and the
   earlier ones were overwritten.
5. For each entry:
   - direction = `flags & 1` (W or R);
   - spill past the page edge = `pa + len - ((pa | (NBPP-1)) + 1)` bytes. These bytes went to
     or came from the physically next frame, not the next virtual page;
   - **does `pa` name a kernel static?** Below 0x40000000 the kernel runs identity-mapped
     (DTT0, `src/z3660.c` VSECT1 comment, DI-1 §4.4). So a `pa` inside the loaded image,
     `[base, base + end)`, is also the VA. `pa - base` looked up in the same map names the
     buffer, for example a static 512-byte RDB buffer in `getrdb`. A `pa` outside the image
     is a kmem or page frame. The driver cannot tell which caller owned it.
   - `block`/`len` match the request against a disk-side capture (fragment reads, the 4096 B
     read) by LBA and size.

### Worked example (illustrative bytes, not a capture)

```
+0x004: 00000003 00000000 00000006 00000002 00000200 08123F00 08123F00 00000200
```

Read as: seq 3, READ, unit 6, LBA 2, 512 B, from pa 0x08123F00, which is the whole request
(`pa == req`, reqlen 512). On a 4 KiB line `0xF00 + 0x200 = 0x1100 > 0x1000`, so the last
0x100 bytes landed in the physically next frame. 0x08123F00 lies inside the image, so
`0x123F00` in the map names the buffer.
