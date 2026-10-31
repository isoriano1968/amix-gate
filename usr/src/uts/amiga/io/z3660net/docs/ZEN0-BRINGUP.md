# Bringing up `zen0` on Amix — the field-proven procedure

**Who this is for:** you have an Amiga running Amix on a Z3660 card whose firmware is the
**Amix-interop fork** (`jusii/Z3660-amix`), and you want the box on the LAN.

**Provenance.** Every command below was run on real hardware on **2026-09-09** — an Amiga 4000
with a Z3660 (card 3), a real **68LC060**, **Amix 2.1c**, kernel `68060-260828-50` — and the
five-command sequence in §2 worked **first try**. The same sequence also works on the emulated
`UAE_040_MMU` path. The campaign's full evidence file is
`Amix/tmp/2026-09-08-card3-amix-image/DEVSD-GROUND-TRUTH.md` (durable scratch, not a repo — which
is why the durable parts of it live here).

**Evidence markers used throughout:**

| mark | meaning |
|---|---|
| ✅ | **Verified from source** — checked in this repo or a sibling repo, path and line given |
| 📋 | **Measured on metal 2026-09-09** — a result recorded in `DEVSD-GROUND-TRUTH.md`; not re-checkable from source |
| 🟡 | **Reported, not verified** — asserted by the campaign; no artifact in this repo or the referenced tree confirms it |

---

## 1. ★ The central fact: `aen` can never work under this firmware, so `zen0` is the only path

Amix ships Commodore's own ethernet driver, **`aen`**, for the A2065 Zorro II card. Under the
Amix-interop Z3660 firmware fork **it can never bind**, no matter how the firmware is configured.
This is not a tuning problem and there is no key that fixes it. Both ends are settled from source.

### 1.1 What the Amix kernel's `aen` driver hunts

`aen` probes lazily, on the first `open()`, by asking Amiga Zorro autoconfig for one specific
board id:

- ✅ `docs/ETHERNET-SCOPING-evidence.md:1191` records it from the driver source:
  `aenautoconfig()` (`aen.c:1421`) calls `autocon(productcode, …)` with **`AEN_BOARD =
  0x02020070`** (`aen.h:66-67`).
- 📋 The same constant read out of the *shipped kernel binary* — `aenautoconfig` at
  `.text+0x15DE6` of `unix-060-fpe-r13-CARD1-ced0s1`:

  ```
    15e14:  2f3c 0202 0070    movel #0x02020070,%sp@-
    15e1a:  jsr autocon
  ```

  (`DEVSD-GROUND-TRUTH.md` §13.1.)

The id decomposes as **manufacturer `0x0202` = 514 = Commodore**, **product `0x0070` = 112 =
A2065**. Two independent readings — the driver source and the binary that actually boots — agree.

### 1.2 What the Amix-interop firmware presents

- ✅ Every emulated autoconfig board the fork exposes — the Z2 SCSI board, the Z3 RAM board, the
  Z3 RTG/ethernet combo — answers with one manufacturer id:

  ```c
  #define MANUF_ID 0x144B // Double H Tech
  ```

  `Z3660/z3660-firmware/Z-TURN/vitis_ide/Z3660_emu/src/cpu_emulator.cpp:263`, fed to the
  `er_Manufacturer` nibble responders at `:294-297` and `:357-360`.

- ✅ And the embedded UAE core has **no A2065 emulation compiled in at all**:

  ```c
  /* #define A2065 */ /* A2065 Ethernet card */
  ```

  `Z3660/z3660-firmware/Z-TURN/vitis_ide/Z3660_emu/src/uae/include/sysconfig.h:79` — the define is
  commented out.

  (Both verified against branch `amix-main-2`, head `e31e734`.)

### 1.3 The conclusion, and why no config key reaches it

`aen -S` hunts `0x02020070` (Commodore/A2065). The firmware offers only `0x144B…` (Double H Tech),
and it does not emulate an A2065 at all. **A manufacturer id is not settable by any `autoconfig_*`
config key** — the keys choose *which boards are enabled and where they land*, not *who made them*.

📋 That is exactly why the probe was measured returning **1 identically** with `autoconfig_rtg NO`
**and** with `autoconfig_rtg YES` (`DEVSD-GROUND-TRUTH.md` §13.3, §14.3). The negative result under
both settings is the measurement that closes the door on configuration as a remedy.

⇒ **`zen0` / `z3660eth` is the only ethernet path under this firmware.** It hunts ✅
`Z3660_PROD 0x144B0001` (`src/z3660eth.h:15`) — the id the firmware actually presents, and the same
board id the `z3660scsi` driver uses to mount root on the very same boot.

### 1.4 ⚠ COROLLARY — "that box networks on `aen0`" does not cross a firmware boundary

This cost the campaign several hours, so it is stated as a rule rather than a footnote.

📋 The project's own *dev SD* box shows a working `aen0` with 3413 input packets — and its Amix
kernel is **byte-identical** to ours, so it is the same `aen` driver hunting the same
`0x02020070`. Since a manufacturer id cannot be configured, **that firmware build must present an
A2065 façade that the Amix-interop fork does not.**

🟡 **That last inference is not verified** and must not be allowed to harden: neither that build's
source nor its running image was available to check (`DEVSD-GROUND-TRUTH.md` §13.4 states the
limit in the same terms). The proven half is the half about *our* firmware, which is proven from
source in §1.2.

**The operational rule:** an observation of the form *"box X networks on `aen0`"* is evidence about
**box X's firmware build**, and carries **no** information about yours. Check which firmware is
running before importing any networking conclusion from another card, another SD, or another
person's notes. The same error in the other direction — reasoning from a firmware's behaviour to a
driver conclusion — is what produced the wrong recommendation that this section replaces.

---

## 2. The bring-up — five commands

Run these on the box as root. Single-user is fine. 📋 Verified working **first try** on real
68LC060 metal on 2026-09-09, and also on the emulated `UAE_040_MMU` path
(`DEVSD-GROUND-TRUTH.md` §14.3).

```sh
mknod /dev/zen0 c 51 0
/usr/sbin/slink
/usr/sbin/slink addaen /dev/zen0 zen0
/usr/sbin/ifconfig zen0 <ip> netmask <mask> up -trailers
/usr/sbin/route add default <gw> 1
```

With the project's own addresses that is `ifconfig zen0 192.168.2.39 netmask 255.255.255.0 up
-trailers` and `route add default 192.168.2.1 1`.

`userland/bringup.sh` in this repo is the same sequence with a `zen -S` presence gate in front and
the values factored out to variables.

### 2.1 The notes that matter

**cdevsw major is 51, and `/dev/zen0` does not exist until something creates it.**
✅ `driver.conf` pins the major (`net z3660eth z3660eth.c z3660ethinfo 51 zen …`), and `README.md`
and `BUILD.md` §2(b) carry the reasoning: 51 is the lowest slot of the ecosystem-verified-free set
51–67/69, renumbered from 48 on 2026-07-30 because 48 turned out to be contested. Amix ships
`/dev/aen0` and `/dev/aen1` but nothing for `zen0`, so the node is yours to make. `mknod` printing
**`File exists`** on a re-run is harmless — that is the node already being there, and the
`[ -c /dev/zen0 ] ||` guard in the boot script exists to suppress it.

**The bare `slink`, on its own line, before `slink addaen`, is load-bearing.** ✅ This is a real-HW
finding from 2026-06-21, recorded in this repo at `userland/S99zen:10-14` and
`GRIMOIRE-HANDOFF.md` §7.4: on a fast clean boot `slink addaen` can race ahead of the base inet
STREAMS plumbing. The proven manual bring-up always ran a bare `slink` **first** to (re)link the
base streams, then `slink addaen`. Do not fold the two into one command and do not drop the first.

**The bare `slink` prints an error when the base streams are already linked, and that error is
correct behaviour.** 🟡 The exact text observed on 2026-09-09 was:

```
Function "tp", command 3: link: Device busy
```

(The wording is from the 2026-09-09 field session; this repo's prior note at `userland/S99zen:14`
records the same condition more loosely as *"'Device busy' from the bare slink is expected and
harmless"*.) **It means the base inet STREAMS are already up — which is the good case. Do not
"fix" it.** Suppressing it, or making the script bail on it, removes the very race protection the
line exists to provide.

**`-trailers` is not optional decoration** — it disables BSD trailer encapsulation, and the stock
`aen0` bring-up passes it too.

**The trailing `1` on `route add default` is the metric, and it is mandatory** on SVR4.0 — it is
not an option flag (✅ `docs/ETHERNET-SCOPING-evidence.md:1681` table, from the golden image's
`rc.inet:42`).

**This is SVR4.0: there is no `ifconfig plumb`.** The STREAMS multiplexor link *is* `slink addaen`.

---

## 3. Making it persist — `/etc/rc2.d/S99zen`

Amix's stock boot chain (`/etc/rc2.d/S69inet` → `/etc/init.d/inetinit` → `/etc/inet/network-config`)
only knows how to bring up `aen0`, and §1 says that can never succeed here. So `zen0` needs its own
rc2 script.

The shipped file is **`/etc/rc2.d/S99zen`**, mode **744**, owner **root**:

- ✅ **371 bytes**, and ✅ **`sum -r` = `56264     1`** — both re-verified in this session against
  the shipped copy at `Amix/tmp/2026-09-08-card3-amix-image/netfix/S99zen.min`.
- 🟡 The **744 / root** mode-and-owner is the on-box installed state as reported; the host-side
  copy carries the host's own mode, so it is not checkable from the file.

### 3.1 The exact shipped body

Reproduced verbatim so it survives the scratch directory being cleared. Note it has **no shebang
and no comments** — that is what makes it 371 bytes and what the checksum above is of. (The
annotated variant with the full comment header is `userland/S99zen` in this repo; it is the same
logic, it is just a different number of bytes.)

```sh
[ -c /dev/zen0 ] || mknod /dev/zen0 c 51 0
(
  sleep 5
  /usr/sbin/slink
  i=0
  while [ $i -lt 20 ]; do
    /usr/sbin/slink addaen /dev/zen0 zen0 && break
    sleep 3
    i=`expr $i + 1`
  done
  /usr/sbin/ifconfig zen0 192.168.2.39 netmask 255.255.255.0 up -trailers
  /usr/sbin/route add default 192.168.2.1 1
  /usr/sbin/ifconfig zen0
) > /tmp/zen-bringup.log 2>&1 &
```

Substitute your own address, netmask and gateway on the `ifconfig` and `route` lines.

### 3.2 Why it is shaped like that — three deliberate choices

1. **It retries `slink addaen` up to 20 times, 3 s apart.** The STREAMS stack is not ready the
   instant `rc2` runs. ✅ Same 2026-06-21 race as §2.1; the loop plus the leading bare `slink` is
   the mitigation (`userland/S99zen:10-14`, loop at `:19-26`; internal provenance
   `GRIMOIRE-HANDOFF.md` §7.4, §9.2 — gitignored, not part of this repo). The initial `sleep 5`
   gives the base streams a head start before the first attempt. Worst case the loop spends ~60 s and gives up — in the
   background, where it costs nothing.

2. **The whole bring-up is backgrounded (`( … ) &`).** ✅ `userland/S99zen:6-8`: a datapath problem
   must never be able to hang the boot. The box still reaches login even if `zen0` never comes up.
   On a board-less box the driver simply `ENXIO`s and the script is harmless.

3. **It logs to `/tmp/zen-bringup.log`.** Because it is backgrounded, nothing it prints would reach
   the console anyway — so it all goes to a file you can read afterwards. **This is the first thing
   to read when `zen0` did not come up.**

### 3.3 ⚠ Use a literal address — never `` `uname -n` ``

The stock `network-config` form is `` ifconfig aen0 `uname -n` up -trailers ``. **Do not copy that
shape.** ✅ `docs/ETHERNET-SCOPING-evidence.md:1684-1685` documents why, from the golden image's own
8-line in-file comment: with DNS enabled, `` `uname -n` `` forces a `gethostbyname()` that tries DNS
**first**, but this runs *before* the default route exists (the route is set later, in `rc.inet`),
so every lookup burns the full resolver retransmit — **~90 s per lookup**, measured elsewhere as a
boot going from 210 s to 29 s once the literal IP replaced the name. A literal IP skips name
resolution entirely.

This applies to `S99zen` exactly as it applies to `network-config`, and it is why every address in
this repo's userland files is spelled out.

### 3.4 Measured bring-up times

🟡 Reported from the 2026-09-09 session; no log in the referenced evidence tree records these, so
treat them as order-of-magnitude expectations rather than a spec:

| path | networked at |
|---|---|
| real 68060 | **t+72 s** from power-on |
| emulated 040 | **t+156 s** from power-on |

The practical consequence is the one this repo has warned about since 2026-06: ✅ *"it can take the
full boot for `zen0` to appear — don't conclude 'broken' from an early check"* (`userland/S99zen:15-16`).
If the SD image is dirty and `fsck` forces a reboot, it takes longer still.

---

## 4. Identity — three files, and `setuname` is not one of them

`zen0` carries the address, but the box's *identity* still has to be right, or name-based access
(telnet by name, RPC, anything that resolves the local host) misbehaves. 📋 All three files below
were read out of the working image and diffed against a non-working one; they differed **only** in
the identity values (`DEVSD-GROUND-TRUTH.md` §12.1–12.3, kept intact by §13.5 and §14.3 after the
`aen0`→`zen0` reversal).

**1. `/etc/nodename`** — the whole file is the name, one line:

```
z3660amix
```

**2. `/etc/inet/hosts`** — one row for the box. Tabs, not spaces, between the address and the names
(that is what the shipped images use):

```
#ident	"@(#)cmd-inet:etc/hosts	1.1"
#
# Internet host table
#
0.0.0.0		anyhost
127.0.0.1	localhost
192.168.2.39	z3660amix z3660amix.alanara.fi
```

A current image carries one more row that is **not** part of the identity: the package repository
host, `pkg.amigaux.org` — the canonical Amix package channel (plain HTTP for `apkg`) since
2026-09-14. The images are DNS-free, so firstboot bakes that host's address into this file; the
address and the firstboot step are owned by [amix-installng](https://github.com/jusii/amix-installng) (its profiles' `FB_REPO_HOST` /
`FB_REPO_IP`), not by this repo. Leave that row alone when re-addressing the box.

**3. ★ `/etc/net/{ticlts,ticots,ticotsord}/hosts`** — the one that is easy to miss. Three separate
files, one per local transport, each `@(#)rpcsvc:net_files/hosts 1.2`, each holding:

```
# RPC Hosts
z3660amix	z3660amix
```

**All three copies must be updated.** 📋 This was flagged in the install plan as *"a manual step,
not a firstboot one"* and then confirmed by bytes on both images.

### 4.1 ★ `setuname -n` is NOT needed and does NOT persist the nodename

State this plainly, because at least two project documents got it wrong and one of them said the
opposite: that the nodename is patched into the kernel file and must be re-applied after any kernel
replacement.

📋 The bytes refute it for this kernel line (`DEVSD-GROUND-TRUTH.md` §12.4):

- The working box's **boot-slice kernel is byte-identical** to the non-working one
  (sha `49a2c78f…`), and it contains **neither** `z3660amix` **nor** the other box's name —
  there is no hostname string in it at all.
- Yet the working box reports `z3660amix`.

⇒ **The nodename is applied at runtime from `/etc/nodename`.** It is not baked into the kernel.

And `setuname` would patch **`/stand/unix`**, which on this line is the untouched 1992 stock kernel
and is **not what boots**. So running it is not merely unnecessary — it edits the wrong file.

Also not needed for the interface to answer: **`/etc/domain` and `/etc/resolv.conf`**. They exist on
both images and they govern DNS resolution for *outbound* lookups, not the local interface address.

---

## 5. Which path is this box on? — a five-check triage

When a box's networking is not working, establish which of the three states it is in before
changing anything.

| check | `zen0` path (correct, this firmware) | stock `aen0` path | nothing configured |
|---|---|---|---|
| `ifconfig -a` | `lo0` **and `zen0`** | `lo0` and `aen0` | **`lo0` only** |
| `ls -l /dev/zen0` | present, char **51**, 0 | absent | absent |
| `ls -l /etc/rc2.d/S99zen` | present, 744 root | absent | absent |
| `cat /etc/inet/network-config` | the `aen` chain, unused/removed | the `aen -S && …` chain | stock `aen` chain |
| `cat /tmp/zen-bringup.log` | the successful `ifconfig zen0` dump | — | — |

`lo0` **only**, on a box running the Amix-interop firmware, means the stock `aen` chain ran and
short-circuited — go to §5.1. Add `/usr/amiga/bin/zen` (built from `userland/zen.c`) to the toolkit
if it is installed: `zen -S` is a silent presence gate (exit 0/1) and bare `zen` dumps the driver's
TX/RX/error counters.

### 5.1 ⚠ Why a broken network produces NO console output at all

The stock `/etc/inet/network-config` bring-up is a three-command `&&` chain (✅
`docs/ETHERNET-SCOPING-evidence.md:1665`):

```sh
/usr/amiga/bin/aen -S &&
    /usr/sbin/slink addaen /dev/aen0 aen0 &&
        /usr/sbin/ifconfig aen0 192.168.2.38 up -trailers
```

**When the probe fails, this chain prints nothing whatsoever.** Two things combine:

- ✅ **`-S` means silent.** The presence tools suppress all output in `-S` mode by design and
  communicate only through the exit status. This repo's own `zen.c` shows the convention exactly —
  `userland/zen.c:31-39` guards the only `fprintf` with `if (!silent)` and then `exit(1)`. The
  evidence dossier records the same for the stock tools: hydra's `hya -S` is *"exit 0 if a board
  found else 1, **silent**"* (`docs/ETHERNET-SCOPING-evidence.md:1356`), and `aen -S` is *"the
  presence gate … exit 0 only if a board answers"* (`:1670`).
- **A short-circuited `&&` chain is silent by construction.** `cmd1 && cmd2` where `cmd1` exits
  non-zero simply does not run `cmd2`, and the shell says nothing about it.

⇒ On a box running the Amix-interop firmware, `aen -S` returns 1 (§1), the chain stops on the first
command, and **the console shows nothing** — no error, no warning, no interface. The boot looks
completely normal.

**This is the trap:** the silence reads as *"the script never ran"*, and it sends you hunting for a
missing or unexecutable `network-config`, a broken `S69inet` hardlink, or a boot-order problem —
none of which is wrong. The script ran perfectly and did exactly what it was written to do. **The
absence of output is the diagnosis, not the absence of a diagnosis.** Confirm it directly by
running `/usr/amiga/bin/aen -S; echo $?` by hand: a bare `1` with no message is the signature of
§1, and the fix is §2 + §3, not anything in `network-config`.

---

## 6. Related files in this repo

- `userland/bringup.sh` — the §2 sequence as a runnable script, with a `zen -S` gate.
- `userland/S99zen` — the §3 boot script, annotated (same logic, more comments than the 371-byte
  shipped `S99zen.min`).
- `userland/network-config.zen` — the `/etc/inet/network-config` snippet, for a box that prefers to
  hang `zen0` off the stock chain rather than use a separate rc2 script. Note its in-file warning
  about editing `network-config` in place: it is hardlinked with `S69inet`/`inetinit`, so never
  unlink and recreate it, and never leave an `S*`-named backup in `rc2.d/` (the `for f in
  /etc/rc2.d/S*` glob would execute it).
- `userland/zen.c` — the `zen` presence/status tool (`/usr/amiga/bin/zen`).
- `BUILD.md` — building the driver into a kernel; §2(b) has the major-51 reasoning.
- `docs/ETHERNET-SCOPING-evidence.md` §§1–3 — the source-grounded dossier on how the stock `aen0`
  comes up, where the static IP/hostname/netmask/gateway live, and what `slink addaen` actually does
  (it links the driver stream under **two** multiplexors, IP *and* ARP).

> ⚠ **`GRIMOIRE-HANDOFF.md` was a frozen 2026-06 handoff document that until 2026-09-09 still
> said `mknod /dev/zen0 c 48 0`** (§6, §9.2) — 48 was stale, superseded by the 2026-07-30
> renumber to **51**. It has since been corrected in place (its own top-of-document correction
> note explains why); the authoritative value remains `driver.conf`, `README.md` and this file.
> Its `slink`/race material (§7.4, §9.2) was and still is current and is cited above.
