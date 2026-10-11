# Burnout 3 Browser Port, Milestones 0–3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get Burnout 3: Takedown (PS2, NTSC-U) booting and playable in Chrome through static recompilation, with a one-time "point the browser at your ISO" step and a private deployment Mark and Scott can open.

**Architecture:** A fork of snuri00/ps2-web-recomp (MIT) does the recompile-to-WebAssembly and supplies the PS2 runtime; it is cloned and built inside WSL, outside this repo. This repo holds Burnout-specific scripts, our page shell (disc picker + Origin Private File System cache + gamepad tweaks), tests, and docs. Game-derived files (ISO, ELF, generated C++, built WASM) never enter the repo tree.

**Tech Stack:** WSL2 Ubuntu 24.04, clang, CMake, Ninja, Emscripten 6.0.11 (installed by the fork's `scripts/setup.sh`), wgpu-native (headless), Python 3 (unittest), Node 20+ (`node:test`), Chrome with WebGPU, Vercel static hosting.

**Spec:** `docs/superpowers/specs/2026-10-10-burnout3-browser-port-design.md`

## Global Constraints

- Target disc: NTSC-U `SLUS_210.50`; ISO SHA-1 `11a7f335a37d2f3f5c13b967f3072c84f8eded02`, size `2867822592` bytes. Local copy: see `docs/LOCAL_PATHS.md`.
- Never commit: ISO, ELF, VU microcode, BIOS, generated C++, built `.wasm`/`.data`. No `.gitignore` (Mark's rule); keep those files under `~/b3/` inside WSL, never under the repo. Check `git status` before every commit.
- Repo license MIT. Any file adapted from the fork keeps a header comment: `// Adapted from ps2-web-recomp (c) 2026 Samir Nuri, MIT. See docs/research/PROVENANCE.md`. Update PROVENANCE.md in the same commit that first adapts or runs an external project.
- Fork: `https://github.com/BlissVibes/ps2-web-recomp`, branch `burnout3`, cloned at `~/b3/ps2-web-recomp` in WSL. Generic runtime fixes go to the fork; Burnout-specific config stays here.
- Deployed build is private (Vercel Authentication). The repo is public.
- Disk: keep everything under `~/b3` below 10 GB; the WSL distro lives on `S:\WSL`.
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus <noreply@anthropic.com>
  ```
  (the Opus windows substitute their own model name).
- Paths in this plan: `REPO` = `/mnt/c/Users/Mark/Documents/_vibe coding projects/Burnout3Decomp` inside WSL (quote it, it has a space); `FORK` = `~/b3/ps2-web-recomp`.

## Review Focus

1. **Interrupted first-run copy.** Closing the tab while the ISO copies into browser storage must not leave a truncated disc that boots to garbage next visit; the page must ask for the ISO again. Test in Task 11.
2. **Wrong disc picked.** A PAL Burnout 3, another game, or a non-ISO file must produce a message naming what was found, not a hang on a black canvas. Tests in Tasks 3 and 10.
3. **Gamepad stick drift.** A DualSense at rest reports small nonzero axes; without a dead zone Burnout's car creeps at menus and veers in races. Test in Task 8.
4. **Storage quota refused.** Chrome may refuse 2.9 GB of OPFS on a nearly full drive; the page must say how much is needed and still let the player play from the picked file this visit. Test in Task 11.
5. **Log scan false negatives.** The headless log scanner is the milestone-1 gate; a new error string the runtime emits that the scanner does not know would pass silently. Task 4 pins every known string and prints unknown bracketed tags.

---

## File structure

```
Burnout3Decomp/
  README.md                         what this is, legal line, how to build, how Scott opens it (Task 13)
  external/FORK.md                  fork URL, branch, pinned commit, how to update (Task 2)
  tools/
    wsl-setup.sh                    installs packages, clones the fork, runs its setup.sh (Task 1, 2)
    verify-iso.py                   checks a disc image is SLUS_210.50 (Task 3)
    scan-log.py                     summarizes a headless run log; exit 1 on gaps (Task 4)
    port.sh                         wraps FORK/scripts/port.sh for Burnout (Task 5)
    build-web.sh                    builds the page with our shell into ~/b3/out/web (Task 7)
    deploy/vercel.json              COOP/COEP headers for the static deploy (Task 13)
    deploy/deploy.sh                copies the build + vercel.json to ~/b3/out/deploy and runs vercel (Task 13)
  tests/
    test_verify_iso.py              (Task 3)
    test_scan_log.py                (Task 4)
    disc.test.mjs                   (Task 10, 11)
  web/
    shell.html                      our Emscripten shell, adapted from the fork's (Task 7, 8, 12)
    disc.js                         ISO identification + OPFS cache (Task 10, 11)
  docs/
    LOCAL_PATHS.md                  (exists)
    PROGRESS.md                     milestone log with numbers (Task 5, 9, 13)
    re/boot-log-001.md ...          each headless run's scan (Task 5, 6)
    re/runtime-gaps.md              every runtime gap found and how it was fixed (Task 6)
```

Fork changes (branch `burnout3` of `BlissVibes/ps2-web-recomp`), kept small and generic:
- `CMakeLists.txt`: `PS2_WEB_SHELL` cache variable (Task 7).
- `host/web.cpp`: `ps2web_fetch` reads from `Module.discFile` when present (Task 12).
- Whatever runtime gaps Burnout exposes (Task 6).

---

## Milestone 0: Toolchain

### Task 1: WSL Ubuntu on S: with build packages

**Files:**
- Create: `tools/wsl-setup.sh`

**Interfaces:**
- Produces: a WSL distro named `Ubuntu-24.04` whose home has `~/b3/`, with `clang++`, `cmake`, `ninja`, `python3`, `node`, `7z`, `curl`, `unzip`, `git` on PATH.

- [ ] **Step 1: Install the distro (Mark runs this, it needs admin and may reboot)**

In an elevated PowerShell on Windows:
```powershell
wsl --install -d Ubuntu-24.04
```
Follow the prompt to create a Linux user named `mark`. If Windows asks to reboot, reboot and run the command again to finish.

- [ ] **Step 2: Move the distro to S:**

```powershell
wsl --shutdown
New-Item -ItemType Directory -Force S:\WSL | Out-Null
wsl --manage Ubuntu-24.04 --move S:\WSL\Ubuntu-24.04
wsl -l -v
```
Expected: `Ubuntu-24.04` listed, `VERSION 2`. If `--manage --move` is not recognized, run `wsl --update` first and retry.

- [ ] **Step 3: Write the setup script**

`tools/wsl-setup.sh`:
```bash
#!/usr/bin/env bash
# Prepares the WSL Ubuntu box for the Burnout 3 browser port.
# Run inside WSL:  bash "/mnt/c/Users/Mark/Documents/_vibe coding projects/Burnout3Decomp/tools/wsl-setup.sh"
set -euo pipefail

FORK_URL="https://github.com/BlissVibes/ps2-web-recomp.git"
FORK_BRANCH="burnout3"
B3="$HOME/b3"
FORK="$B3/ps2-web-recomp"

echo "== packages"
sudo apt-get update -qq
sudo apt-get install -y -qq git cmake ninja-build clang lld python3 python3-pip curl unzip p7zip-full nodejs npm pkg-config libsdl2-dev

echo "== layout"
mkdir -p "$B3/iso" "$B3/out"

if [ ! -d "$FORK/.git" ]; then
  echo "== cloning the fork"
  git clone --branch "$FORK_BRANCH" "$FORK_URL" "$FORK"
else
  echo "== fork already cloned"
fi

echo "== fork toolchain (emsdk, wgpu-native, eerecomp, vurecomp)"
"$FORK/scripts/setup.sh"

echo "== versions"
clang++ --version | head -1
cmake --version | head -1
ninja --version
python3 --version
node --version
7z | head -2 | tail -1
ls -la "$FORK/build/tools/eerecomp" "$FORK/build/tools/vurecomp"
echo "== ready: fork at $FORK, work dir $B3"
```

- [ ] **Step 4: Run the package half only (the fork does not exist yet; Task 2 creates it)**

Inside WSL:
```bash
sudo apt-get update -qq && sudo apt-get install -y -qq git cmake ninja-build clang lld python3 python3-pip curl unzip p7zip-full nodejs npm pkg-config libsdl2-dev
clang++ --version | head -1; cmake --version | head -1; ninja --version; node --version
```
Expected: clang 18.x, cmake 3.28+, ninja 1.11+, node 18+ printed. If `node --version` is below 18, run `curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash - && sudo apt-get install -y nodejs`.

- [ ] **Step 5: Commit**

```bash
cd "$REPO" && git add tools/wsl-setup.sh && git commit -m "tools: WSL setup script for the build box"
```

### Task 2: Fork ps2-web-recomp, pin it, build its tools

**Files:**
- Create: `external/FORK.md`
- Modify: `docs/research/PROVENANCE.md` (ps2-web-recomp row: use → `derived`)

**Interfaces:**
- Produces: `~/b3/ps2-web-recomp` on branch `burnout3`; `~/b3/ps2-web-recomp/build/tools/eerecomp` and `vurecomp` executables; `~/b3/ps2-web-recomp/tools/emsdk` with Emscripten 6.0.11.

- [ ] **Step 1: Create the fork on GitHub (from Windows, `gh` is authenticated as BlissVibes)**

```bash
gh repo fork snuri00/ps2-web-recomp --org BlissVibes --clone=false
```
If the account is a user not an org, drop `--org BlissVibes`. Expected: `✓ Created fork BlissVibes/ps2-web-recomp`.

- [ ] **Step 2: Clone in WSL, create the branch**

```bash
mkdir -p ~/b3 && git clone https://github.com/BlissVibes/ps2-web-recomp.git ~/b3/ps2-web-recomp
cd ~/b3/ps2-web-recomp && git remote add upstream https://github.com/snuri00/ps2-web-recomp.git
git checkout -b burnout3 && git push -u origin burnout3
git rev-parse HEAD
```
Record the printed commit hash for Step 4.

- [ ] **Step 3: Build the fork's tools**

```bash
cd ~/b3/ps2-web-recomp && scripts/setup.sh
```
Expected (takes 5–15 minutes the first time): ends with `== ready`. Then:
```bash
ls -la build/tools/eerecomp build/tools/vurecomp && du -sh tools/emsdk third_party
```
Expected: both executables exist; emsdk about 1.5 GB.

- [ ] **Step 4: Write the pin file**

`external/FORK.md`:
```markdown
# ps2-web-recomp fork

- Upstream: https://github.com/snuri00/ps2-web-recomp (MIT, (c) 2026 Samir Nuri)
- Our fork: https://github.com/BlissVibes/ps2-web-recomp, branch `burnout3`
- Pinned commit: `<hash from Step 2>` (upstream `17a5eee43eb013d43f26ace6dbff5139e5e8043c`, 2026-10-11)
- Local clone (WSL): `~/b3/ps2-web-recomp`

Why not a submodule: the fork's `port.sh` writes `games/`, `generated/` and `build/`
inside its own tree. Those hold game-derived files that must never enter this repo,
so the fork lives outside the repo and is pinned here instead.

## Update procedure
1. `cd ~/b3/ps2-web-recomp && git fetch upstream && git rebase upstream/main`
2. Rebuild: `scripts/setup.sh`, then rerun `tools/port.sh` from this repo.
3. If the headless run still passes `tools/scan-log.py`, `git push --force-with-lease`,
   update the pinned commit above, and note the upstream date.

## Our changes on `burnout3` (keep generic, offer upstream)
- (none yet)
```

- [ ] **Step 5: Update PROVENANCE and commit**

In `docs/research/PROVENANCE.md`, change the ps2-web-recomp row's Use column from `reference (candidate: derived)` to `derived (fork at external/FORK.md)`.
```bash
cd "$REPO" && git add external/FORK.md docs/research/PROVENANCE.md && git commit -m "external: pin BlissVibes/ps2-web-recomp fork; mark as derived in provenance"
```

### Task 3: ISO verifier

**Files:**
- Create: `tools/verify-iso.py`
- Test: `tests/test_verify_iso.py`

**Interfaces:**
- Produces: `python3 tools/verify-iso.py <iso> [--sha1]` exits 0 and prints `OK SLUS_210.50` for the right disc; exits 1 with `boots <X>, expected SLUS_210.50` otherwise. Module functions `boot_program(f) -> str | None`, `root_dir(f) -> (lba, size)`.

- [ ] **Step 1: Write the failing test**

`tests/test_verify_iso.py`:
```python
import importlib.util, os, struct, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('verify_iso', os.path.join(HERE, '..', 'tools', 'verify-iso.py'))
verify_iso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_iso)

SECTOR = 2048


def dir_record(name: bytes, lba: int, size: int, flags: int = 0) -> bytes:
    body = bytearray(b'\x00')
    body += struct.pack('<I', lba) + struct.pack('>I', lba)
    body += struct.pack('<I', size) + struct.pack('>I', size)
    body += bytes(7)
    body += bytes([flags, 0, 0]) + struct.pack('<H', 1) + struct.pack('>H', 1)
    body += bytes([len(name)]) + name
    if len(name) % 2 == 0:
        body += b'\x00'
    return bytes([len(body) + 1]) + bytes(body)


def make_iso(path: str, system_cnf: bytes | None) -> None:
    """A minimal ISO 9660 image: PVD at sector 16, root directory at 18, SYSTEM.CNF at 19."""
    with open(path, 'wb') as f:
        f.write(bytes(16 * SECTOR))
        pvd = bytearray(SECTOR)
        pvd[0] = 1
        pvd[1:6] = b'CD001'
        pvd[6] = 1
        struct.pack_into('<I', pvd, 80, 20)
        struct.pack_into('>I', pvd, 84, 20)
        pvd[156:156 + 34] = dir_record(b'\x00', 18, SECTOR, 2)
        f.write(pvd)
        f.write(bytes(SECTOR))  # sector 17: terminator left blank
        root = bytearray(SECTOR)
        off = 0
        for rec in (dir_record(b'\x00', 18, SECTOR, 2), dir_record(b'\x01', 18, SECTOR, 2)):
            root[off:off + len(rec)] = rec
            off += len(rec)
        if system_cnf is not None:
            rec = dir_record(b'SYSTEM.CNF;1', 19, len(system_cnf))
            root[off:off + len(rec)] = rec
        f.write(root)
        f.write((system_cnf or b'').ljust(SECTOR, b'\x00'))


class VerifyIsoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, 'disc.iso')

    def tearDown(self):
        self.tmp.cleanup()

    def test_boot_program_reads_boot2(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLUS_210.50;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n')
        with open(self.path, 'rb') as f:
            self.assertEqual(verify_iso.boot_program(f), 'SLUS_210.50')

    def test_other_disc_is_reported(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLES_525.84;1\r\nVER = 1.00\r\nVMODE = PAL\r\n')
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('boots SLES_525.84', message)

    def test_missing_system_cnf(self):
        make_iso(self.path, None)
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('no SYSTEM.CNF', message)

    def test_not_an_iso(self):
        with open(self.path, 'wb') as f:
            f.write(b'MZ' + bytes(40000))
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('not an ISO 9660 image', message)

    def test_right_disc_ok(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLUS_210.50;1\r\n')
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual((code, message), (0, 'OK SLUS_210.50'))


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd "$REPO" && python3 -m unittest tests.test_verify_iso -v
```
Expected: errors loading `tools/verify-iso.py` (file not found).

- [ ] **Step 3: Write the verifier**

`tools/verify-iso.py`:
```python
#!/usr/bin/env python3
"""Checks that a disc image is Burnout 3: Takedown, NTSC-U (boots SLUS_210.50).

usage: verify-iso.py <image.iso> [--sha1]
Exit 0 when the image boots SLUS_210.50, 1 otherwise. --sha1 also hashes the
whole image (about a minute) and compares it with the known-good release.
"""
import hashlib
import struct
import sys

SECTOR = 2048
EXPECTED_BOOT = 'SLUS_210.50'
EXPECTED_SHA1 = '11a7f335a37d2f3f5c13b967f3072c84f8eded02'
EXPECTED_SIZE = 2867822592


def read_sectors(f, first, count=1):
    f.seek(first * SECTOR)
    return f.read(count * SECTOR)


def root_dir(f):
    pvd = read_sectors(f, 16)
    if pvd[:6] != b'\x01CD001':
        raise ValueError('not an ISO 9660 image (no primary volume descriptor at sector 16)')
    return struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]


def find_in_dir(f, lba, size, wanted):
    data = read_sectors(f, lba, (size + SECTOR - 1) // SECTOR)
    off = 0
    while off < size:
        length = data[off]
        if length == 0:
            off = (off // SECTOR + 1) * SECTOR
            continue
        rec = data[off:off + length]
        off += length
        name = rec[33:33 + rec[32]].decode('ascii', 'replace').split(';')[0].upper()
        if name == wanted:
            return struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
    return None


def boot_program(f):
    lba, size = root_dir(f)
    entry = find_in_dir(f, lba, size, 'SYSTEM.CNF')
    if not entry:
        return None
    text = read_sectors(f, entry[0], (entry[1] + SECTOR - 1) // SECTOR)[:entry[1]].decode('ascii', 'replace')
    for line in text.splitlines():
        key, _, value = line.partition('=')
        if key.strip() == 'BOOT2':
            return value.strip().split('\\')[-1].split(';')[0]
    return None


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 24), b''):
            h.update(chunk)
    return h.hexdigest()


def check(path, want_sha1):
    try:
        with open(path, 'rb') as f:
            try:
                boot = boot_program(f)
            except ValueError as e:
                return 1, str(e)
    except OSError as e:
        return 1, 'cannot open %s: %s' % (path, e.strerror)
    if boot is None:
        return 1, 'no SYSTEM.CNF with a BOOT2 line in the root directory'
    if boot != EXPECTED_BOOT:
        return 1, 'boots %s, expected %s' % (boot, EXPECTED_BOOT)
    if want_sha1:
        digest = sha1_of(path)
        if digest != EXPECTED_SHA1:
            return 1, 'boots %s but SHA-1 %s differs from the known release %s' % (boot, digest, EXPECTED_SHA1)
    return 0, 'OK %s' % boot


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    code, message = check(args[0], '--sha1' in argv)
    print(message)
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run the tests**

```bash
cd "$REPO" && python3 -m unittest tests.test_verify_iso -v
```
Expected: 5 tests, `OK`.

- [ ] **Step 5: Run it on the real disc (from WSL) and copy the disc into the work dir**

```bash
python3 "$REPO/tools/verify-iso.py" "/mnt/s/_GAME ISOS/Burnout 3/Burnout 3 - Takedown.iso"
cp "/mnt/s/_GAME ISOS/Burnout 3/Burnout 3 - Takedown.iso" ~/b3/iso/burnout3.iso
python3 "$REPO/tools/verify-iso.py" ~/b3/iso/burnout3.iso --sha1
```
Expected: `OK SLUS_210.50` both times. The copy makes every later read fast (the `/mnt/s` path goes through a slow network filesystem).

- [ ] **Step 6: Commit**

```bash
cd "$REPO" && git add tools/verify-iso.py tests/test_verify_iso.py && git commit -m "tools: ISO verifier for SLUS_210.50 with tests"
```

---

## Milestone 1: Burnout boots headless

### Task 4: Log scanner

**Files:**
- Create: `tools/scan-log.py`
- Test: `tests/test_scan_log.py`

**Interfaces:**
- Produces: `python3 tools/scan-log.py <explore.log>` prints a summary and exits 0 when the run reached its last frame with no runtime gaps, 1 otherwise. Function `scan(lines) -> Report` where `Report` has `gaps: dict[str, Counter]`, `halted: str | None`, `unknown_tags: Counter`, `frames: int | None`.

The runtime's known gap strings (from the fork's source, verified 2026-10-10):

| Key | Line prefix | Source |
|---|---|---|
| `unhandled_syscall` | `[sys] unhandled syscall 0x.. from 0x........` | `runtime/src/kernel.cpp` |
| `iop_missing` | `[iop] missing <lib> #<n> called from 0x........` | `runtime/src/iop/kernel.cpp` |
| `ipu_unimplemented` | `[ipu] command 0x........ is not implemented` | `runtime/src/ipu/ipu.cpp` |
| `vu1_cmsar1` | `[vu1] start at 0x.. through CMSAR1 (not implemented)` | `runtime/src/cop.cpp` |
| `io_unsupported` | `[io] open <path> (flags 0x..) on unsupported device` | `runtime/src/iop/io.cpp` |
| halt | `[ee] halted: <reason>` | `runtime/src/system.cpp` |

- [ ] **Step 1: Write the failing test**

`tests/test_scan_log.py`:
```python
import importlib.util, os, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('scan_log', os.path.join(HERE, '..', 'tools', 'scan-log.py'))
scan_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan_log)

CLEAN = """[disc] disc: 1200 files, 1400304 sectors, layer 1 at 0
[web] presenting through WebGPU
[ee] frame 3600
"""

GAPPY = """[disc] disc: 1200 files, 1400304 sectors, layer 1 at 0
[sys] unhandled syscall 0x7A from 0x00123456
[sys] unhandled syscall 0x7A from 0x00123456
[iop] missing libsd #22 called from 0x000A1B2C
[ipu] command 0x12345678 is not implemented
[vu1] start at 0x40 through CMSAR1 (not implemented)
[io] open host0:/foo (flags 0x1) on unsupported device
[zzz] something new the scanner has never seen
[ee] halted: jump to unmapped 0x00000000
"""


class ScanLogTest(unittest.TestCase):
    def test_clean_run_is_clean(self):
        report = scan_log.scan(CLEAN.splitlines(True))
        self.assertEqual(report.halted, None)
        self.assertEqual(sum(sum(c.values()) for c in report.gaps.values()), 0)
        self.assertTrue(report.clean())

    def test_gaps_are_counted_by_distinct_site(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.gaps['unhandled_syscall']['0x7A from 0x00123456'], 2)
        self.assertEqual(report.gaps['iop_missing']['libsd #22 called from 0x000A1B2C'], 1)
        self.assertEqual(report.gaps['ipu_unimplemented']['0x12345678'], 1)
        self.assertEqual(report.gaps['vu1_cmsar1']['0x40'], 1)
        self.assertEqual(report.gaps['io_unsupported']['host0:/foo'], 1)
        self.assertEqual(report.halted, 'jump to unmapped 0x00000000')
        self.assertFalse(report.clean())

    def test_unknown_bracket_tags_are_surfaced(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.unknown_tags['zzz'], 1)
        self.assertNotIn('disc', report.unknown_tags)

    def test_render_mentions_every_gap_and_the_halt(self):
        text = scan_log.render(scan_log.scan(GAPPY.splitlines(True)))
        for needle in ('unhandled_syscall', '0x7A from 0x00123456', 'x2', 'libsd #22', 'halted: jump to unmapped', 'unknown tags: zzz'):
            self.assertIn(needle, text)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd "$REPO" && python3 -m unittest tests.test_scan_log -v
```
Expected: fails loading `tools/scan-log.py`.

- [ ] **Step 3: Write the scanner**

`tools/scan-log.py`:
```python
#!/usr/bin/env python3
"""Summarizes a ps2run / port.sh explore log and says whether the run was clean.

usage: scan-log.py <explore.log>
Exit 0 when no runtime gap was logged and the EE did not halt, 1 otherwise.
The known gap strings come from ps2-web-recomp's runtime; any other
"[tag] ..." line the scanner has not seen is listed under "unknown tags" so a
new error string cannot pass silently.
"""
import collections
import re
import sys

PATTERNS = {
    'unhandled_syscall': re.compile(r'^\[sys\] unhandled syscall (0x[0-9A-Fa-f]+ from 0x[0-9A-Fa-f]+)'),
    'iop_missing': re.compile(r'^\[iop\] missing (\S+ #\d+ called from 0x[0-9A-Fa-f]+)'),
    'ipu_unimplemented': re.compile(r'^\[ipu\] command (0x[0-9A-Fa-f]+) is not implemented'),
    'vu1_cmsar1': re.compile(r'^\[vu1\] start at (0x[0-9A-Fa-f]+) through CMSAR1'),
    'io_unsupported': re.compile(r'^\[io\] open (\S+) .* on unsupported device'),
}
HALT = re.compile(r'^\[ee\] halted: (.*)$')
TAG = re.compile(r'^\[([a-z0-9]+)\]')
KNOWN_TAGS = {'sys', 'iop', 'ipu', 'vu1', 'io', 'ee', 'disc', 'web', 'gpu', 'gs', 'spu2', 'dma', 'sif', 'mc', 'vu0', 'vif', 'cdvd', 'kernel', 'thread'}
FRAME = re.compile(r'^\[ee\] frame (\d+)')


class Report:
    def __init__(self):
        self.gaps = {key: collections.Counter() for key in PATTERNS}
        self.halted = None
        self.unknown_tags = collections.Counter()
        self.frames = None

    def clean(self):
        return self.halted is None and not any(self.gaps.values())


def scan(lines):
    report = Report()
    for raw in lines:
        line = raw.rstrip('\r\n')
        halt = HALT.match(line)
        if halt:
            report.halted = halt.group(1)
            continue
        frame = FRAME.match(line)
        if frame:
            report.frames = int(frame.group(1))
            continue
        matched = False
        for key, pattern in PATTERNS.items():
            m = pattern.match(line)
            if m:
                report.gaps[key][m.group(1)] += 1
                matched = True
                break
        if matched:
            continue
        tag = TAG.match(line)
        if tag and tag.group(1) not in KNOWN_TAGS:
            report.unknown_tags[tag.group(1)] += 1
    return report


def render(report):
    out = []
    total = sum(sum(c.values()) for c in report.gaps.values())
    out.append('frames reached: %s' % (report.frames if report.frames is not None else 'unknown'))
    out.append('halted: %s' % (report.halted or 'no'))
    out.append('gaps: %d distinct, %d total' % (sum(len(c) for c in report.gaps.values()), total))
    for key, counter in report.gaps.items():
        for site, count in counter.most_common():
            out.append('  %s  %s  x%d' % (key, site, count))
    if report.unknown_tags:
        out.append('unknown tags: ' + ', '.join('%s x%d' % kv for kv in report.unknown_tags.most_common()))
    out.append('result: ' + ('CLEAN' if report.clean() else 'GAPS'))
    return '\n'.join(out)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[1], encoding='utf-8', errors='replace') as f:
        report = scan(f)
    print(render(report))
    return 0 if report.clean() else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run the tests**

```bash
cd "$REPO" && python3 -m unittest tests.test_scan_log -v
```
Expected: 4 tests, `OK`.

- [ ] **Step 5: Commit**

```bash
cd "$REPO" && git add tools/scan-log.py tests/test_scan_log.py && git commit -m "tools: headless run log scanner with tests"
```

### Task 4b: Scanner gate hardening (from Opus 2's review)

**Files:**
- Modify: `tools/scan-log.py`
- Modify: `tests/test_scan_log.py`

**Interfaces:**
- Produces: `scan-log.py <log> [--expect-frames N]`. `Report.clean(expect_frames=None)` is false when `frames is None` or `frames < expect_frames` (a log that just stops is not clean), and false when `unknown_tags` is non-empty (a new runtime message cannot pass silently; add real benign tags to `KNOWN_TAGS` as the first runs reveal them). `render` prints `frames reached: N of M` when an expectation is given.

- [ ] **Step 1: Add failing tests**

Append to `tests/test_scan_log.py` inside the class:
```python
    def test_a_log_that_just_stops_is_not_clean_when_frames_are_expected(self):
        report = scan_log.scan(CLEAN.splitlines(True))          # reaches frame 3600
        self.assertTrue(report.clean(expect_frames=3600))
        self.assertFalse(report.clean(expect_frames=7200))
        truncated = scan_log.scan(CLEAN.splitlines(True)[:2])    # no frame line at all
        self.assertFalse(truncated.clean(expect_frames=3600))
        self.assertTrue(truncated.clean())                       # no expectation given: old behaviour

    def test_unknown_tags_make_the_run_not_clean(self):
        report = scan_log.scan("[zzz] new message\n[ee] frame 100\n".splitlines(True))
        self.assertFalse(report.clean())
        self.assertIn('unknown tags: zzz', scan_log.render(report))

    def test_main_exit_codes(self):
        import io, os, tempfile
        with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
            f.write(CLEAN)
        try:
            self.assertEqual(scan_log.main(['scan-log.py', f.name]), 0)
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '3600']), 0)
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '9999']), 1)
        finally:
            os.unlink(f.name)
```

- [ ] **Step 2: Run to see them fail** — `python -m unittest tests.test_scan_log -v` → 3 failures (`clean() got an unexpected keyword`, exit code 0 instead of 1, etc.).

- [ ] **Step 3: Implement**

In `tools/scan-log.py`:
```python
    def clean(self, expect_frames=None):
        if self.halted is not None or any(self.gaps.values()) or self.unknown_tags:
            return False
        if expect_frames is not None and (self.frames is None or self.frames < expect_frames):
            return False
        return True
```
`render(report, expect_frames=None)`: first line becomes `'frames reached: %s%s' % (frames or 'none', ' of %d' % expect_frames if expect_frames else '')`, and the last line uses `report.clean(expect_frames)`. `main`: parse `--expect-frames N` (int) from argv, pass it to `render` and to the exit decision. Also redirect `main`'s prints so the test can call it: keep `print` but it is harmless in tests.

- [ ] **Step 4: Run tests** → 7 OK. **Step 5: Commit** `tools: scan-log gate requires expected frames and rejects unknown tags`.

Task 5's wrapper then calls `python3 "$REPO/tools/scan-log.py" "$FORK/games/$NAME/explore.log" --expect-frames "$FRAMES"` where `FRAMES` defaults to 3600 and is set from a `--frames N` argument before the rest is passed through.

### Task 5: First headless port run

**Files:**
- Create: `tools/port.sh`
- Create: `docs/re/boot-log-001.md`
- Create: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: `~/b3/iso/burnout3.iso` (Task 3), `~/b3/ps2-web-recomp/scripts/port.sh` (Task 2), `tools/scan-log.py` (Task 4).
- Produces: `~/b3/ps2-web-recomp/games/burnout3/` (disc link, `entries.txt`, `vu1/`, `shots/`, `explore.log`), `~/b3/ps2-web-recomp/generated/burnout3/*.cpp`, `~/b3/ps2-web-recomp/build/native-burnout3/ps2run`.

- [ ] **Step 1: Write the wrapper**

`tools/port.sh`:
```bash
#!/usr/bin/env bash
# Recompiles Burnout 3 with the fork's port.sh and scans the exploring run's log.
# Run inside WSL. Optional args are passed through to the fork's port.sh
# (for example --frames 1800 --press 300:start --press 900:cross).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$HOME/b3/ps2-web-recomp"
ISO="$HOME/b3/iso/burnout3.iso"
NAME=burnout3

[ -f "$ISO" ] || { echo "no disc at $ISO; see docs/LOCAL_PATHS.md and Task 3" >&2; exit 1; }
python3 "$REPO/tools/verify-iso.py" "$ISO"

# Headless runs in WSL have no reliable Vulkan device; PS2_GPU_FALLBACK lets the
# runtime use the fallback adapter rather than fail (the fork reads it in gpu.cpp).
export PS2_GPU_FALLBACK=1
FRAMES=3600
if [ "${1:-}" = "--frames" ]; then FRAMES="$2"; shift 2; fi
cd "$FORK"
scripts/port.sh "$NAME" "$ISO" --frames "$FRAMES" "$@" 2>&1 | tee "$HOME/b3/out/port-$(date +%Y%m%d-%H%M%S).log"

echo
echo "== scan of the last exploring run"
python3 "$REPO/tools/scan-log.py" "$FORK/games/$NAME/explore.log" --expect-frames "$FRAMES" || true
echo "frames in $FORK/games/$NAME/shots:"; ls "$FORK/games/$NAME/shots" | tail -5
```

- [ ] **Step 2: Run it (first run compiles everything; allow 15–40 minutes)**

```bash
chmod +x "$REPO/tools/port.sh" && "$REPO/tools/port.sh" --frames 1800
```
Expected shape of output: `== [1/6] disc`, `boots cdrom0:\SLUS_210.50;1`, `== [3/6] machine code to C++` with a function count, `== [4/6] exploring` with `round 1: ... N new entry points`, then rounds until `0 new entry points` or a halt, then `[5/6]` and `[6/6] web build`, then the scan. If `[4/6]` fails to compile, print the last 30 lines of `~/b3/ps2-web-recomp/build/native-burnout3/ninja.log` into the boot log below and stop; that is a Fable task.

- [ ] **Step 3: Look at the frames**

```bash
ls -la ~/b3/ps2-web-recomp/games/burnout3/shots/
```
Open the last few PNGs from Windows Explorer at `\\wsl$\Ubuntu-24.04\home\mark\b3\ps2-web-recomp\games\burnout3\shots\`. Note what is on screen (black, EA logo, Criterion logo, press-start, memory-card prompt, main menu).

- [ ] **Step 4: Record the run**

`docs/re/boot-log-001.md`:
```markdown
# Headless run 001 — <date>

Fork commit: <hash>. Command: `tools/port.sh --frames 1800`.

## eerecomp
<paste the `[3/6]` summary line: functions / units / bytes>

## Exploring rounds
<paste each `round N: ...` line>

## Scan
```
<paste tools/scan-log.py output>
```

## Frames
<for the last 5 shots: frame number → what is visible>

## Verdict
<CLEAN and reached menus / GAPS: list for Fable / build failure: ninja.log tail>
```

`docs/PROGRESS.md`:
```markdown
# Progress

| Date | Milestone | State | Notes |
|---|---|---|---|
| <date> | 0 Toolchain | done | WSL on S:, fork built |
| <date> | 1 Headless boot | <running / blocked on gaps / done> | see docs/re/boot-log-001.md |
```

- [ ] **Step 5: Commit**

```bash
cd "$REPO" && git status --short   # confirm only tools/port.sh, docs/re/boot-log-001.md, docs/PROGRESS.md
git add tools/port.sh docs/re/boot-log-001.md docs/PROGRESS.md && git commit -m "tools: Burnout port wrapper; record headless run 001"
```

### Task 6: Runtime gap loop (repeat until the scan is CLEAN past the main menu)

This task is a procedure, run once per gap. **Diagnosis and the fix design are Fable's work; the Opus window reproduces, applies the fix Fable specifies, re-runs and records.** If the Opus window can see the fix itself (a missing IOP library function with an obvious ps2sdk signature, for example), it may do it and say so in the record.

**Files:**
- Modify: files under `~/b3/ps2-web-recomp/runtime/` (in the fork, branch `burnout3`)
- Create/append: `docs/re/runtime-gaps.md`
- Create: `docs/re/boot-log-00N.md` per run

**Interfaces:**
- Consumes: the scan from the previous run.
- Produces: a fork commit per gap; the scan of the next run.

- [ ] **Step 1: Reproduce the gap in isolation, fast**

```bash
cd ~/b3/ps2-web-recomp
PS2_GPU_FALLBACK=1 build/native-burnout3/ps2run games/burnout3/disc/SLUS_210.50 --disc ~/b3/iso/burnout3.iso \
  --frames 1800 --press 300:start --press 600:cross --press 900:cross --frames-dir games/burnout3/shots --frame-every 300 \
  --trace-syscalls --trace-iop-hle 2>&1 | tee ~/b3/out/repro.log | tail -50
```
Hand `~/b3/out/repro.log` (or its relevant 200 lines) to Fable with the scan line for the gap. Useful extra traces: `--trace-iop` (module loads), `--trace-threads`, `PS2_TRACE_DMA=1`.

- [ ] **Step 2: Apply the fix in the fork**

Edit the file Fable names. Build only the native runner and re-run the repro:
```bash
ninja -C build/native-burnout3 ps2run && <the Step 1 command>
python3 "$REPO/tools/scan-log.py" ~/b3/out/repro.log
```
Expected: the gap's line is gone from the scan; no new gap appears earlier than the frame the old one hit.

- [ ] **Step 3: Commit in the fork**

```bash
cd ~/b3/ps2-web-recomp && git add -p && git commit -m "iop: implement <lib> #<n> (<name>) for Burnout 3" && git push
```
Keep each fix to one commit, generic (no Burnout-specific constants), so it can be offered upstream.

- [ ] **Step 4: Re-run the full port and record**

```bash
"$REPO/tools/port.sh" --frames 3600
```
Write `docs/re/boot-log-00N.md` in the Task 5 format. Append to `docs/re/runtime-gaps.md`:
```markdown
## <gap key> <site>  — fixed <date>, fork commit <hash>
- Symptom: <what the frames showed / what the log said>
- Cause: <one or two sentences>
- Fix: <file, function, what it does>
- Upstreamable: yes / no (why)
```
Update `external/FORK.md` "Our changes on burnout3" with one line per commit.

- [ ] **Step 5: Commit the records**

```bash
cd "$REPO" && git add docs/re external/FORK.md docs/PROGRESS.md && git commit -m "re: runtime gap <key> <site> fixed; headless run 00N"
```

**Exit condition for milestone 1:** `tools/scan-log.py` prints `result: CLEAN` on a 3600-frame run whose shots show the main menu (or beyond). Mark milestone 1 done in `docs/PROGRESS.md`.

---

## Milestone 2: Playable in Chrome

### Task 7: Our own page shell, built by our script

**Files:**
- Modify (fork): `~/b3/ps2-web-recomp/CMakeLists.txt` lines around `--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html`
- Create: `web/shell.html` (copy of the fork's `web/shell.html`, with a provenance header)
- Create: `tools/build-web.sh`
- Modify: `external/FORK.md`

**Interfaces:**
- Produces: `~/b3/out/web/ps2web.html`, `ps2web.js`, `ps2web.wasm`, `ps2web.data` plus `.gz` copies, built from `web/shell.html` in this repo. CMake cache variable `PS2_WEB_SHELL` (FILEPATH, default `${CMAKE_SOURCE_DIR}/web/shell.html`).

- [ ] **Step 1: Add `PS2_WEB_SHELL` to the fork**

In `~/b3/ps2-web-recomp/CMakeLists.txt`, inside `if(EMSCRIPTEN)` under `if(PS2_GENERATED)`, add after the `PS2_WEB_BOOT_FILE` line:
```cmake
        set(PS2_WEB_SHELL "${CMAKE_SOURCE_DIR}/web/shell.html" CACHE FILEPATH "Emscripten shell HTML for the page")
```
and change the two references:
```cmake
            --shell-file ${PS2_WEB_SHELL} --preload-file ${PS2_WEB_ELF}@/game/core.elf)
        set_target_properties(ps2web PROPERTIES SUFFIX ".html" LINK_DEPENDS ${PS2_WEB_SHELL})
```
Commit and push in the fork: `git commit -am "cmake: PS2_WEB_SHELL lets a port supply its own shell page" && git push`.

- [ ] **Step 2: Copy the shell into our repo with a header**

```bash
cp ~/b3/ps2-web-recomp/web/shell.html "$REPO/web/shell.html"
```
Insert as the second line of `web/shell.html` (after `<!doctype html>`):
```html
<!-- Adapted from ps2-web-recomp web/shell.html, (c) 2026 Samir Nuri, MIT. See docs/research/PROVENANCE.md -->
```
Change `<title>ps2-web-recomp</title>` to `<title>Burnout 3 in the browser</title>`.

- [ ] **Step 3: Write the build script**

`tools/build-web.sh`:
```bash
#!/usr/bin/env bash
# Builds the browser page for Burnout 3 with this repo's web/shell.html.
# Output: ~/b3/out/web (outside the repo on purpose: it contains the game's code).
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FORK="$HOME/b3/ps2-web-recomp"
NAME=burnout3
OUT="$HOME/b3/out/web"
EMSDK="${EMSDK:-$FORK/tools/emsdk}"

boot=$(sed -n 's/^BOOT2 *= *//p' "$FORK/games/$NAME/disc/SYSTEM.CNF" | tr -d '\r')
program=${boot#*\\}; program=${program%;*}
elf="$FORK/games/$NAME/disc/$program"
[ -f "$elf" ] || { echo "no executable at $elf; run tools/port.sh first" >&2; exit 1; }

mkdir -p "$OUT"
printf '%s\n' "$boot" > "$OUT/boot.txt"
source "$EMSDK/emsdk_env.sh" > /dev/null 2>&1
emcmake cmake -S "$FORK" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPS2_GENERATED="$FORK/generated/$NAME" -DPS2_WEB_ELF="$elf" -DPS2_WEB_BOOT_FILE="$OUT/boot.txt" \
  -DPS2_WEB_SHELL="$REPO/web/shell.html" > /dev/null
ninja -C "$OUT" ps2web
cp "$REPO"/web/*.js "$OUT"/ 2>/dev/null || true
gzip -kf9 "$OUT/ps2web.wasm" "$OUT/ps2web.data" "$OUT/ps2web.js"
ls -la "$OUT"/ps2web.* 
echo "built $OUT/ps2web.html"
echo "serve:  python3 $FORK/scripts/serve.py $OUT ~/b3/iso/burnout3.iso   then open http://localhost:8620/"
```

- [ ] **Step 4: Build and open it**

```bash
chmod +x "$REPO/tools/build-web.sh" && "$REPO/tools/build-web.sh"
python3 ~/b3/ps2-web-recomp/scripts/serve.py ~/b3/out/web ~/b3/iso/burnout3.iso
```
In Chrome on Windows open `http://localhost:8620/` (WSL ports forward to localhost). Expected: title "Burnout 3 in the browser", status shows `Disc 2.67 GB, GPU nvidia ...`, Start enabled; after Start the game boots as the headless run did. Record the `ps2web.wasm` size from the `ls` output in `docs/PROGRESS.md` (it matters for hosting limits).

- [ ] **Step 5: Commit**

Update `external/FORK.md` "Our changes" with the CMake commit. Then:
```bash
cd "$REPO" && git add web/shell.html tools/build-web.sh external/FORK.md docs/PROGRESS.md && git commit -m "web: our shell page and build script using the fork's PS2_WEB_SHELL"
```

### Task 8: Gamepad dead zone and DualSense check

**Files:**
- Modify: `web/shell.html` (the `axis` helper and gamepad loop inside `pollInput`)
- Create: `web/pad.js`
- Test: `tests/pad.test.mjs`

**Interfaces:**
- Produces: `web/pad.js` exporting `applyDeadZone(x, y, radius = 0.08) -> {x, y}` (radial, rescaled so output reaches ±1 at the rim) and `toPs2Axis(v) -> 0..255`. `shell.html` imports nothing (Emscripten shell is a classic script), so `pad.js` is loaded with a `<script src="pad.js">` tag before the main script and defines `window.B3Pad = { applyDeadZone, toPs2Axis }`.

- [ ] **Step 1: Write the failing test**

`tests/pad.test.mjs`:
```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { applyDeadZone, toPs2Axis } = createRequire(import.meta.url)('../web/pad.js');

test('rest position inside the dead zone is exactly centred', () => {
  assert.deepEqual(applyDeadZone(0.03, -0.05), { x: 0, y: 0 });
});

test('full deflection still reaches the rim', () => {
  const { x, y } = applyDeadZone(1, 0);
  assert.ok(Math.abs(x - 1) < 1e-9 && y === 0);
});

test('just outside the dead zone starts near zero, not at the zone edge', () => {
  const { x } = applyDeadZone(0.09, 0);
  assert.ok(x > 0 && x < 0.02, `got ${x}`);
});

test('diagonal is not clipped per axis', () => {
  const { x, y } = applyDeadZone(0.7071, 0.7071);
  assert.ok(Math.abs(x - y) < 1e-6 && Math.hypot(x, y) <= 1 + 1e-9);
});

test('ps2 axis mapping', () => {
  assert.equal(toPs2Axis(0), 128);
  assert.equal(toPs2Axis(-1), 0);
  assert.equal(toPs2Axis(1), 255);
  assert.equal(toPs2Axis(2), 255);
});
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd "$REPO" && node --test tests/pad.test.mjs
```
Expected: fails, cannot find `../web/pad.js`.

- [ ] **Step 3: Write pad.js**

`web/pad.js`:
```js
// Gamepad helpers for the Burnout 3 page. Loaded as a classic script by shell.html
// and importable as a module by the tests.
(function (root) {
  function applyDeadZone(x, y, radius = 0.08) {
    const len = Math.hypot(x, y);
    if (len <= radius) return { x: 0, y: 0 };
    const scaled = Math.min(1, (len - radius) / (1 - radius));
    return { x: (x / len) * scaled, y: (y / len) * scaled };
  }
  function toPs2Axis(v) {
    return Math.max(0, Math.min(255, Math.round((v + 1) * 127.5)));
  }
  const api = { applyDeadZone, toPs2Axis };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Pad = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
```
The file is a plain script with a CommonJS fallback, which is why the test loads it through `createRequire` rather than an ES import.

- [ ] **Step 4: Run the tests**

```bash
cd "$REPO" && node --test tests/pad.test.mjs
```
Expected: 5 passing.

- [ ] **Step 5: Use it in the shell**

In `web/shell.html`, before `<script>` that defines `statusEl`, add `<script src="pad.js"></script>`. Replace the `axis` helper and the gamepad block in `pollInput`:
```js
const axis = (v) => B3Pad.toPs2Axis(v);
...
  for (const pad of navigator.getGamepads ? navigator.getGamepads() : []) {
    if (!pad) continue;
    for (const [index, bit] of padButtons) if (pad.buttons[index] && pad.buttons[index].pressed) buttons |= 1 << bit;
    if (pad.axes.length >= 4) {
      const left = B3Pad.applyDeadZone(pad.axes[0], pad.axes[1]);
      const right = B3Pad.applyDeadZone(pad.axes[2], pad.axes[3]);
      if (lx === 128) lx = axis(left.x);
      if (ly === 128) ly = axis(left.y);
      rx = axis(right.x);
      ry = axis(right.y);
    }
    break;
  }
```
`tools/build-web.sh` already copies `web/*.js` next to the page.

- [ ] **Step 6: Manual check with the DualSense**

Rebuild and serve (Task 7 Step 4). Plug in the DualSense by USB. In Chrome DevTools console run `navigator.getGamepads()[0].mapping` → expected `"standard"`. Press each face button at the "press start" / menu screens and confirm: cross confirms, triangle backs, D-pad and left stick both move the menu cursor, Start pauses in a race. With the stick at rest, the menu cursor must not drift. Note the result in `docs/PROGRESS.md`.

- [ ] **Step 7: Commit**

```bash
cd "$REPO" && git add web/pad.js web/shell.html tests/pad.test.mjs docs/PROGRESS.md && git commit -m "web: radial dead zone for gamepad sticks; DualSense verified"
```

### Task 9: Playable gate

**Files:**
- Modify: `docs/PROGRESS.md`

- [ ] **Step 1: Play a race**

From the served page: boot, decline or accept the memory card prompt, reach the main menu, pick a single race, finish one lap. Watch the status bar: `fps` value and `on screen`. Expected: ≥59 fps at the 640×512 setting on the RTX 3090 Ti; music and engine audio present; no `[sys]`/`[iop]` lines in the DevTools console.

- [ ] **Step 2: Record and decide**

In `docs/PROGRESS.md` add a row for milestone 2 with: fps at 1x and 2x, audio yes/no, any visual glitch (name the screen). If fps < 59 at 1x or audio is missing, that is a Fable task: file it in `docs/re/runtime-gaps.md` as an open item and still proceed to milestone 3 (the disc flow does not depend on it).

- [ ] **Step 3: Commit**

```bash
cd "$REPO" && git add docs/PROGRESS.md docs/re/runtime-gaps.md && git commit -m "progress: milestone 2 playable check"
```

---

## Milestone 3: Load-once disc and private deploy

### Task 10: Disc identification in the browser

**Files:**
- Create: `web/disc.js`
- Test: `tests/disc.test.mjs`

**Interfaces:**
- Produces: `web/disc.js` as a classic script defining `window.B3Disc` with `bootProgram(blob) -> Promise<string|null>` and `identify(blob) -> Promise<{boot, ok}>` where `ok` is `boot === 'SLUS_210.50'`. Also exports via `module.exports` for the tests (same shim pattern as `pad.js`). `blob` is any `Blob`/`File` (the test uses a `Blob` built from a synthetic ISO).

- [ ] **Step 1: Write the failing test**

`tests/disc.test.mjs`:
```js
import test from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const { bootProgram, identify, EXPECTED_BOOT } = createRequire(import.meta.url)('../web/disc.js');

const SECTOR = 2048;
function u32le(v) { return [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255]; }
function u32be(v) { return u32le(v).reverse(); }
function dirRecord(name, lba, size, flags = 0) {
  const nameBytes = [...name].map((c) => c.charCodeAt(0));
  const body = [0, ...u32le(lba), ...u32be(lba), ...u32le(size), ...u32be(size), 0, 0, 0, 0, 0, 0, 0, flags, 0, 0, 1, 0, 0, 1, nameBytes.length, ...nameBytes];
  if (nameBytes.length % 2 === 0) body.push(0);
  return new Uint8Array([body.length + 1, ...body]);
}
export function makeIso(systemCnf) {
  const image = new Uint8Array(20 * SECTOR);
  const pvd = image.subarray(16 * SECTOR, 17 * SECTOR);
  pvd[0] = 1; pvd.set([67, 68, 48, 48, 49], 1); pvd[6] = 1;
  pvd.set(u32le(20), 80); pvd.set(u32be(20), 84);
  pvd.set(dirRecord('\0', 18, SECTOR, 2), 156);
  const root = image.subarray(18 * SECTOR, 19 * SECTOR);
  let off = 0;
  for (const rec of [dirRecord('\0', 18, SECTOR, 2), dirRecord('\u0001', 18, SECTOR, 2)]) { root.set(rec, off); off += rec.length; }
  if (systemCnf !== null) {
    const text = new TextEncoder().encode(systemCnf);
    root.set(dirRecord('SYSTEM.CNF;1', 19, text.length), off);
    image.set(text, 19 * SECTOR);
  }
  return new Blob([image]);
}

test('reads the boot program from SYSTEM.CNF', async () => {
  assert.equal(await bootProgram(makeIso('BOOT2 = cdrom0:\\SLUS_210.50;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n')), 'SLUS_210.50');
});

test('identify accepts only the NTSC-U disc', async () => {
  assert.deepEqual(await identify(makeIso('BOOT2 = cdrom0:\\SLUS_210.50;1\r\n')), { boot: 'SLUS_210.50', ok: true });
  assert.deepEqual(await identify(makeIso('BOOT2 = cdrom0:\\SLES_525.84;1\r\n')), { boot: 'SLES_525.84', ok: false });
  assert.equal(EXPECTED_BOOT, 'SLUS_210.50');
});

test('missing SYSTEM.CNF gives null', async () => {
  assert.equal(await bootProgram(makeIso(null)), null);
});

test('not an ISO throws a readable error', async () => {
  await assert.rejects(bootProgram(new Blob([new Uint8Array(40000)])), /not an ISO 9660 image/);
});

test('a file shorter than 17 sectors throws, does not hang', async () => {
  await assert.rejects(bootProgram(new Blob([new Uint8Array(100)])), /not an ISO 9660 image/);
});
```

- [ ] **Step 2: Run it to verify it fails**

```bash
cd "$REPO" && node --test tests/disc.test.mjs
```
Expected: cannot find `../web/disc.js`.

- [ ] **Step 3: Write disc.js (identification half)**

`web/disc.js`:
```js
// Disc handling for the Burnout 3 page: identify an ISO and cache it in the browser.
// Classic script for shell.html (window.B3Disc), CommonJS for the tests.
(function (root) {
  const SECTOR = 2048;
  const EXPECTED_BOOT = 'SLUS_210.50';
  const CACHE_NAME = 'burnout3.iso';
  const MARKER_NAME = 'burnout3.ok';

  async function bytes(blob, offset, length) {
    return new Uint8Array(await blob.slice(offset, offset + length).arrayBuffer());
  }
  const le32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)) + b[o + 3] * 16777216;
  const ascii = (b) => String.fromCharCode(...b);

  async function bootProgram(blob) {
    const pvd = await bytes(blob, 16 * SECTOR, SECTOR);
    if (pvd.length < 190 || pvd[0] !== 1 || ascii(pvd.subarray(1, 6)) !== 'CD001') throw new Error('not an ISO 9660 image (no volume descriptor at sector 16)');
    const rootLba = le32(pvd, 158), rootSize = le32(pvd, 166);
    const dir = await bytes(blob, rootLba * SECTOR, Math.ceil(rootSize / SECTOR) * SECTOR);
    for (let off = 0; off < rootSize && off < dir.length;) {
      const len = dir[off];
      if (!len) { off = (Math.floor(off / SECTOR) + 1) * SECTOR; continue; }
      const name = ascii(dir.subarray(off + 33, off + 33 + dir[off + 32])).split(';')[0].toUpperCase();
      if (name === 'SYSTEM.CNF') {
        const text = new TextDecoder().decode(await bytes(blob, le32(dir, off + 2) * SECTOR, le32(dir, off + 10)));
        const m = text.match(/^BOOT2\s*=\s*(.+)$/m);
        return m ? m[1].trim().split('\\').pop().split(';')[0] : null;
      }
      off += len;
    }
    return null;
  }

  async function identify(blob) {
    const boot = await bootProgram(blob);
    return { boot, ok: boot === EXPECTED_BOOT };
  }

  const api = { SECTOR, EXPECTED_BOOT, CACHE_NAME, MARKER_NAME, bootProgram, identify };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.B3Disc = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
```

- [ ] **Step 4: Run the tests**

```bash
cd "$REPO" && node --test tests/disc.test.mjs
```
Expected: 5 passing.

- [ ] **Step 5: Commit**

```bash
cd "$REPO" && git add web/disc.js tests/disc.test.mjs && git commit -m "web: identify a Burnout 3 ISO in the browser"
```

### Task 11: Origin Private File System cache

**Files:**
- Modify: `web/disc.js` (add the cache half)
- Modify: `tests/disc.test.mjs` (cache tests against a fake OPFS)

**Interfaces:**
- Produces, on `B3Disc`: `cachedDisc(storage = navigator.storage) -> Promise<File|null>` (null when absent or when the marker says the copy did not finish), `cacheDisc(file, onProgress, storage = navigator.storage) -> Promise<File>` (writes `burnout3.iso`, then `burnout3.ok` holding the byte size; throws `QuotaError` with a message naming GB needed and free before writing anything), `ejectDisc(storage = navigator.storage) -> Promise<void>`. The `storage` parameter is what lets the tests inject a fake.

- [ ] **Step 1: Write the failing tests (append to `tests/disc.test.mjs`)**

```js
// A fake of the slice of the File System Access API that disc.js uses.
function fakeStorage({ quota = 1e12, usage = 0, persist = true } = {}) {
  const files = new Map();
  const handle = (name) => ({
    name,
    async getFile() { const parts = files.get(name) || []; return new File(parts, name); },
    async createWritable() {
      const parts = [];
      return {
        async write(chunk) { parts.push(chunk); },
        async close() { files.set(name, parts); },
        async abort() {},
        // pipeTo support: WritableStream-like adapter
        getWriter() { return { write: async (c) => parts.push(c), close: async () => files.set(name, parts), releaseLock() {} }; },
      };
    },
  });
  const dir = {
    async getFileHandle(name, opts = {}) {
      if (!files.has(name) && !opts.create) { const e = new Error('nf'); e.name = 'NotFoundError'; throw e; }
      if (!files.has(name) && opts.create) files.set(name, []);
      return handle(name);
    },
    async removeEntry(name) { if (!files.delete(name)) { const e = new Error('nf'); e.name = 'NotFoundError'; throw e; } },
  };
  return { files, async getDirectory() { return dir; }, async estimate() { return { quota, usage }; }, async persist() { return persist; } };
}

const { cachedDisc, cacheDisc, ejectDisc, CACHE_NAME, MARKER_NAME } = createRequire(import.meta.url)('../web/disc.js');

test('nothing cached yet', async () => {
  assert.equal(await cachedDisc(fakeStorage()), null);
});

test('cache then load round-trips the bytes and reports progress', async () => {
  const storage = fakeStorage();
  const data = new Uint8Array(3 * 1024 * 1024).map((_, i) => i & 255);
  const seen = [];
  await cacheDisc(new File([data], 'x.iso'), (done, total) => seen.push([done, total]), storage);
  const back = await cachedDisc(storage);
  assert.equal(back.size, data.length);
  assert.deepEqual(new Uint8Array(await back.slice(1000, 1010).arrayBuffer()), data.subarray(1000, 1010));
  assert.equal(seen.at(-1)[0], data.length);
  assert.equal(seen.at(-1)[1], data.length);
});

test('an interrupted copy (no marker, or wrong size) is treated as absent', async () => {
  const storage = fakeStorage();
  storage.files.set(CACHE_NAME, [new Uint8Array(5000)]);          // no marker
  assert.equal(await cachedDisc(storage), null);
  storage.files.set(MARKER_NAME, [new TextEncoder().encode('9999')]);  // marker disagrees with size
  assert.equal(await cachedDisc(storage), null);
  storage.files.set(MARKER_NAME, [new TextEncoder().encode('5000')]);
  assert.equal((await cachedDisc(storage)).size, 5000);
});

test('refuses to start when the quota is too small, naming the sizes', async () => {
  const storage = fakeStorage({ quota: 1000, usage: 500 });
  await assert.rejects(cacheDisc(new File([new Uint8Array(4000)], 'x.iso'), null, storage), (e) => e.name === 'QuotaError' && /need 0\.00 GB/.test(e.message) && /0\.00 GB free/.test(e.message));
  assert.equal(storage.files.size, 0);
});

test('eject removes both files and tolerates absence', async () => {
  const storage = fakeStorage();
  await cacheDisc(new File([new Uint8Array(10)], 'x.iso'), null, storage);
  await ejectDisc(storage);
  assert.equal(storage.files.size, 0);
  await ejectDisc(storage);
});
```

- [ ] **Step 2: Run to verify the new tests fail**

```bash
cd "$REPO" && node --test tests/disc.test.mjs
```
Expected: the 5 identification tests pass, the 5 cache tests fail (`cachedDisc is not a function`).

- [ ] **Step 3: Add the cache half to disc.js**

Inside the IIFE, before `const api`, add:
```js
  async function readMarker(dir) {
    try {
      const file = await (await dir.getFileHandle(MARKER_NAME)).getFile();
      return Number(await file.text());
    } catch (e) {
      if (e.name === 'NotFoundError') return null;
      throw e;
    }
  }

  async function cachedDisc(storage = root.navigator && root.navigator.storage) {
    const dir = await storage.getDirectory();
    let file;
    try {
      file = await (await dir.getFileHandle(CACHE_NAME)).getFile();
    } catch (e) {
      if (e.name === 'NotFoundError') return null;
      throw e;
    }
    const expected = await readMarker(dir);
    if (expected === null || expected !== file.size) return null;
    return file;
  }

  async function writeAll(handle, source, onProgress, total) {
    const writable = await handle.createWritable();
    const writer = writable.getWriter ? writable.getWriter() : writable;
    const reader = source.stream().getReader();
    let done = 0;
    try {
      for (;;) {
        const { value, done: finished } = await reader.read();
        if (finished) break;
        await writer.write(value);
        done += value.byteLength;
        if (onProgress) onProgress(done, total);
      }
      await writer.close();
    } catch (e) {
      if (writer.abort) await writer.abort().catch(() => {});
      throw e;
    }
  }

  async function cacheDisc(file, onProgress, storage = root.navigator && root.navigator.storage) {
    if (storage.persist) await storage.persist().catch(() => {});
    const { quota, usage } = await storage.estimate();
    const free = Math.max(0, quota - usage);
    if (free < file.size) {
      const gb = (n) => (n / 1073741824).toFixed(2);
      const e = new Error('Not enough browser storage: need ' + gb(file.size) + ' GB, ' + gb(free) + ' GB free');
      e.name = 'QuotaError';
      throw e;
    }
    const dir = await storage.getDirectory();
    await dir.removeEntry(MARKER_NAME).catch(() => {});
    await writeAll(await dir.getFileHandle(CACHE_NAME, { create: true }), file, onProgress, file.size);
    await writeAll(await dir.getFileHandle(MARKER_NAME, { create: true }), new Blob([String(file.size)]), null, 0);
    return (await dir.getFileHandle(CACHE_NAME)).getFile();
  }

  async function ejectDisc(storage = root.navigator && root.navigator.storage) {
    const dir = await storage.getDirectory();
    for (const name of [MARKER_NAME, CACHE_NAME]) {
      try { await dir.removeEntry(name); } catch (e) { if (e.name !== 'NotFoundError') throw e; }
    }
  }
```
and extend `api` with `cachedDisc, cacheDisc, ejectDisc`.

Note on the real browser: `createWritable()` on main-thread OPFS handles returns a `FileSystemWritableFileStream`, which has `write`, `close`, `abort` and `getWriter` (it is a `WritableStream`). The code above works with either shape, which is why the fake offers both.

- [ ] **Step 4: Run the tests**

```bash
cd "$REPO" && node --test tests/disc.test.mjs
```
Expected: 10 passing.

- [ ] **Step 5: Commit**

```bash
cd "$REPO" && git add web/disc.js tests/disc.test.mjs && git commit -m "web: cache the disc in the Origin Private File System with an integrity marker"
```

### Task 11b: Disc cache hardening (from Opus 2's review)

**Files:**
- Modify: `web/disc.js`
- Modify: `tests/disc.test.mjs`

**Interfaces:**
- `cacheDisc` removes any existing `burnout3.iso` and `burnout3.ok` **before** the quota check, so replacing a cached disc on a tight drive is not refused for space the old copy holds.
- `bootProgram` refuses a root directory larger than 256 KiB (`rootSize > 131072 * 2`) with `Error('not an ISO 9660 image (root directory too large)')`, so a corrupt image cannot make the page read the whole file.

- [ ] **Step 1: Failing tests** — append to `tests/disc.test.mjs`:
```js
test('re-caching frees the old copy before checking quota', async () => {
  // fake whose usage counts the bytes it holds
  const storage = fakeStorage({ quota: 12000 });
  const base = storage.estimate;
  storage.estimate = async () => ({ quota: 12000, usage: [...storage.files.values()].flat().reduce((n, c) => n + (c.byteLength ?? c.size ?? 0), 0) });
  await cacheDisc(new File([new Uint8Array(8000)], 'a.iso'), null, storage);
  await cacheDisc(new File([new Uint8Array(9000)], 'b.iso'), null, storage);   // would fail if the 8000 still counted
  assert.equal((await cachedDisc(storage)).size, 9000);
});

test('an absurd root directory size is rejected, not read', async () => {
  const image = new Uint8Array(20 * SECTOR);
  const pvd = image.subarray(16 * SECTOR, 17 * SECTOR);
  pvd[0] = 1; pvd.set([67, 68, 48, 48, 49], 1); pvd[6] = 1;
  pvd.set(u32le(18), 158); pvd.set(u32le(0x7fffffff), 166);
  await assert.rejects(bootProgram(new Blob([image])), /root directory too large/);
});
```
- [ ] **Step 2: Run** → the two new tests fail.
- [ ] **Step 3: Implement** — in `cacheDisc`, move the `dir` lookup and a loop removing `MARKER_NAME` then `CACHE_NAME` (ignoring `NotFoundError`) to before `storage.estimate()`. In `bootProgram`, after reading `rootSize`: `if (rootSize > 262144) throw new Error('not an ISO 9660 image (root directory too large)');`.
- [ ] **Step 4: Run** → 12 pass. **Step 5: Commit** `web: disc cache frees the old copy first; cap root directory reads`.

### Task 12: Wire the disc flow into the runtime and the page

**Files:**
- Modify (fork): `~/b3/ps2-web-recomp/host/web.cpp`, the `ps2web_fetch` EM_JS block
- Modify: `web/shell.html` (overlay, `preRun`, eject button)
- Modify: `external/FORK.md`

**Interfaces:**
- Consumes: `B3Disc.identify / cachedDisc / cacheDisc / ejectDisc` (Tasks 10, 11).
- Produces: the runtime reads disc bytes from `Module.discFile` (a `File`) when set, else from `Module.discUrl` as before. `Module.discSize` is set from `discFile.size`.

- [ ] **Step 1: Fork: read from a File when one is present**

In `~/b3/ps2-web-recomp/host/web.cpp`, replace the body of `ps2web_fetch`:
```cpp
EM_JS(void, ps2web_fetch, (const char *url, double offset, int size, void *out, std::atomic<std::uint32_t> *state), {
    const finish = (value) => {
        Atomics.store(HEAP32, state >> 2, value);
        Atomics.notify(HEAP32, state >> 2);
    };
    const store = (buffer, from) => {
        let bytes = new Uint8Array(buffer);
        if (from) bytes = bytes.subarray(from, from + size);
        HEAPU8.set(bytes.subarray(0, Math.min(bytes.length, size)), out);
        finish(3);
    };
    const fail = (e) => {
        console.warn('disc read failed', e);
        finish(4);
    };
    if (Module.discFile) {
        Module.discFile.slice(offset, offset + size).arrayBuffer().then((buffer) => store(buffer, 0)).catch(fail);
        return;
    }
    const target = UTF8ToString(url);
    fetch(target, {headers: {Range: 'bytes=' + offset + '-' + (offset + size - 1)}})
        .then((response) => {
            if (response.status !== 206 && response.status !== 200) throw new Error('status ' + response.status);
            return response.arrayBuffer().then((buffer) => store(buffer, response.status === 200 ? offset : 0));
        })
        .catch(fail);
});
```
Build natively to make sure nothing else broke (`ninja -C build/native-burnout3 ps2run` does not compile web.cpp; the web build in Step 4 is the real check). Commit in the fork: `git commit -am "web: read the disc from Module.discFile when the page supplies one" && git push`.

- [ ] **Step 2: Shell: the first-run overlay**

In `web/shell.html`:

(a) Add `<script src="disc.js"></script>` next to the `pad.js` tag.

(b) Replace the overlay markup:
```html
<div id="overlay"><div id="panel">
  <h1 style="margin:0;font-size:20px">Burnout 3: Takedown</h1>
  <p id="help" hidden></p>
  <p id="disc-text">Checking for a cached disc…</p>
  <button id="pick" type="button" hidden>Choose your Burnout 3 ISO</button>
  <input id="pick-file" type="file" accept=".iso" hidden>
  <progress id="copy" max="100" value="0" hidden style="width:320px"></progress>
  <button id="start" disabled>Loading…</button>
</div></div>
```
Add to the bar, before the fullscreen button: `<button id="eject" type="button" title="Forget the cached disc; the page asks for the ISO again next time">Eject disc</button>`.

(c) Replace the disc part of `preRun`. Where it currently does `Module.addRunDependency('disc'); Promise.all([fetch(Module.discUrl, { method: 'HEAD' }), chooseGpu()...` change to:
```js
    Module.addRunDependency('disc');
    Promise.all([discReady, chooseGpu().catch((e) => 'GPU check failed: ' + e)]).then(([file, gpu]) => {
      Module.discFile = file;
      Module.discSize = file.size;
      Module.gpuInfo = gpu;
      if (Module.gpuHelp && !missing.length) {
        const help = document.getElementById('help');
        help.textContent = Module.gpuHelp;
        help.hidden = false;
      }
      statusEl.textContent = 'Disc ' + (file.size / 1073741824).toFixed(2) + ' GB, ' + gpu;
      Module.removeRunDependency('disc');
    });
```

(d) Add the disc flow before `var Module = {`:
```js
const discText = document.getElementById('disc-text');
const pickButton = document.getElementById('pick');
const pickInput = document.getElementById('pick-file');
const copyBar = document.getElementById('copy');
let resolveDisc;
const discReady = new Promise((resolve) => { resolveDisc = resolve; });

async function findDisc() {
  let cached = null;
  try { cached = await B3Disc.cachedDisc(); } catch (e) { console.warn('disc cache unavailable', e); }
  if (cached) {
    discText.textContent = 'Disc ready (' + (cached.size / 1073741824).toFixed(2) + ' GB cached in this browser).';
    resolveDisc(cached);
    return;
  }
  discText.textContent = 'First time here: point the browser at your Burnout 3: Takedown (NTSC-U) ISO. It is copied into this browser once and never uploaded.';
  pickButton.hidden = false;
}
pickButton.addEventListener('click', () => pickInput.click());
pickInput.addEventListener('change', async () => {
  const file = pickInput.files[0];
  pickInput.value = '';
  if (!file) return;
  pickButton.disabled = true;
  let id;
  try { id = await B3Disc.identify(file); } catch (e) { discText.textContent = file.name + ': ' + e.message; pickButton.disabled = false; return; }
  if (!id.ok) {
    discText.textContent = file.name + ' boots ' + (id.boot || 'nothing recognisable') + '. This page needs the NTSC-U Burnout 3 disc (SLUS_210.50).';
    pickButton.disabled = false;
    return;
  }
  pickButton.hidden = true;
  discText.textContent = 'Disc accepted. You can start now; it is being copied into this browser for next time.';
  resolveDisc(file);                                   // play from the picked file right away
  copyBar.hidden = false;
  try {
    await B3Disc.cacheDisc(file, (done, total) => { copyBar.value = Math.round(done * 100 / total); });
    copyBar.hidden = true;
    discText.textContent = 'Disc cached. Next time this page starts straight away.';
  } catch (e) {
    copyBar.hidden = true;
    discText.textContent = (e.name === 'QuotaError' ? e.message : 'Could not cache the disc: ' + e.message) + ' You can still play this visit.';
  }
});
document.getElementById('eject').addEventListener('click', async () => {
  if (!confirm('Forget the cached disc? The page will ask for your ISO again next time.')) return;
  try { await B3Disc.ejectDisc(); note('Disc ejected'); } catch (e) { note('Eject failed: ' + e.message); }
});
findDisc();
```
Remove `discUrl: params.get('disc') || 'disc.iso',` from `Module`? Keep it: the fork still reads `Module.discUrl` for `ps2web_disc_url()`; leave the line so the string exists.

- [ ] **Step 3: Rebuild the page and serve it without a disc on the server**

```bash
"$REPO/tools/build-web.sh"
python3 ~/b3/ps2-web-recomp/scripts/serve.py ~/b3/out/web ~/b3/out/web/boot.txt
```
The fork's server is still needed for its COOP/COEP headers, but it is pointed at a tiny text file instead of the disc. If the game boots, every disc byte came from the browser, not the server.

- [ ] **Step 4: Manual checks in Chrome (fresh profile so the cache starts empty)**

Launch `chrome.exe --user-data-dir=%TEMP%\b3-profile http://localhost:8620/` from Windows.
1. Overlay says "First time here…", button visible. Pick a wrong file (any small file): message says it is not an ISO 9660 image. Pick a PAL/other ISO if one is at hand: message names the boot program it found.
2. Pick `S:\_GAME ISOS\Burnout 3\Burnout 3 - Takedown.iso`: "Disc accepted", progress bar advances, Start enabled; click Start; game boots while the copy continues. Open DevTools → Application → Storage → confirm usage grows to about 2.7 GB.
3. Reload. Overlay says "Disc ready (2.67 GB cached…)" with no picker. Start boots.
4. Close the tab mid-copy test: eject, reload, pick the ISO again, close the tab at 30%. Reopen: the page must ask for the ISO again (marker missing), not boot a truncated disc.
5. Eject → confirm → reload → picker is back.
6. Memory card: start a race, save when prompted, reload, confirm the save is still there (IDBFS card is untouched by this change).
Record each result in `docs/PROGRESS.md`.

- [ ] **Step 5: Commit**

Update `external/FORK.md` "Our changes" with the web.cpp commit.
```bash
cd "$REPO" && git add web/shell.html external/FORK.md docs/PROGRESS.md && git commit -m "web: load-once disc flow (pick, identify, cache, eject)"
```

### Task 13: Private deploy on Vercel, README

**Files:**
- Create: `tools/deploy/vercel.json`
- Create: `tools/deploy/deploy.sh`
- Create: `README.md`
- Modify: `docs/PROGRESS.md`

**Interfaces:**
- Consumes: `~/b3/out/web` (Task 12).
- Produces: a Vercel project `burnout3-web` on team `mark-wilsons-projects-4b36a71c`, production URL protected by Vercel Authentication, plus a bypass link for Scott.

- [ ] **Step 1: Headers**

`tools/deploy/vercel.json`:
```json
{
  "$schema": "https://openapi.vercel.sh/vercel.json",
  "cleanUrls": false,
  "trailingSlash": false,
  "headers": [
    {
      "source": "/(.*)",
      "headers": [
        { "key": "Cross-Origin-Opener-Policy", "value": "same-origin" },
        { "key": "Cross-Origin-Embedder-Policy", "value": "require-corp" },
        { "key": "Cross-Origin-Resource-Policy", "value": "same-origin" },
        { "key": "Cache-Control", "value": "no-cache" }
      ]
    },
    {
      "source": "/(.*)\\.wasm",
      "headers": [ { "key": "Content-Type", "value": "application/wasm" } ]
    }
  ]
}
```

- [ ] **Step 2: Deploy script**

`tools/deploy/deploy.sh`:
```bash
#!/usr/bin/env bash
# Stages the built page as a static site and deploys it to Vercel (private project).
# Run inside WSL after tools/build-web.sh. Needs `npm i -g vercel` and `vercel login` once.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$HOME/b3/out/web"
STAGE="$HOME/b3/out/deploy"
PROJECT=burnout3-web
TEAM=mark-wilsons-projects-4b36a71c

[ -f "$SRC/ps2web.html" ] || { echo "no build at $SRC; run tools/build-web.sh" >&2; exit 1; }
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp "$SRC"/ps2web.html "$SRC"/ps2web.wasm "$SRC"/ps2web.data "$SRC"/*.js "$STAGE"/
cp "$STAGE/ps2web.html" "$STAGE/index.html"
cp "$REPO/tools/deploy/vercel.json" "$STAGE/vercel.json"
du -sh "$STAGE"; ls -la "$STAGE"
cd "$STAGE"
vercel link --yes --project "$PROJECT" --scope "$TEAM" > /dev/null
vercel deploy --prod --yes --scope "$TEAM"
```
The `ps2web.data` file holds the game's executable and `ps2web.wasm` its translated code, which is why this deploy is private. Never run this against a public project.

- [ ] **Step 3: Deploy**

```bash
command -v vercel >/dev/null || npm i -g vercel
vercel login            # once; opens a browser
chmod +x "$REPO/tools/deploy/deploy.sh" && "$REPO/tools/deploy/deploy.sh"
```
Expected: a `https://burnout3-web-….vercel.app` production URL printed. Vercel's per-file limit is 100 MB; the `ls` output shows the sizes (expected: wasm tens of MB).

- [ ] **Step 4: Protect it (Vercel dashboard, Mark does this once)**

Project → Settings → Deployment Protection:
1. Vercel Authentication: **Enabled**, scope **Standard Protection** (on the Hobby plan this protects the `*.vercel.app` production URL since no custom domain is attached). Visiting the URL now requires logging in as Mark.
2. Protection Bypass for Automation → **Generate secret**. Scott's link is
   `https://<production-url>/?x-vercel-protection-bypass=<secret>&x-vercel-set-bypass-cookie=true`
   The first visit sets a cookie; after that the bare URL works in that browser. Send the link to Scott privately, never commit it.
If Vercel Authentication is not offered for production on this plan, fall back to Cloudflare Pages: `npx wrangler pages deploy ~/b3/out/deploy --project-name burnout3-web`, with a `_headers` file holding the same three headers for `/*`, and Cloudflare Zero Trust → Access → add an application for the pages.dev hostname with a policy allowing the two email addresses (free for up to 50 users). Record which path was taken in `docs/PROGRESS.md`.

- [ ] **Step 5: Verify from a second browser profile**

`chrome.exe --user-data-dir=%TEMP%\b3-scott <bypass link>`: expected the page loads (not the Vercel login), `crossOriginIsolated` is `true` in the console, picker flow from Task 12 Step 4 works, race runs. Then open the bare URL in that profile: still loads (cookie). Open the bare URL in an incognito window: Vercel login page.

- [ ] **Step 6: README**

`README.md`:
```markdown
# Burnout 3: Takedown in the browser

An unofficial fan project that runs the PlayStation 2 game *Burnout 3: Takedown*
(NTSC-U, SLUS-21050) in a modern browser with no install, by statically
recompiling its executable to WebAssembly and progressively replacing that
translated code with our own readable C++. Not affiliated with EA or Criterion.

**You need your own disc.** The page asks once for your ISO, keeps it in your
browser's private storage, and never uploads it. Nothing from the game is in
this repository or on any server we run: not the disc, not the executable, not
code generated from it. The built page is deployed privately for the project's
two developers for the same reason.

## How it works
- [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) (MIT, by Samir Nuri),
  through our fork pinned in `external/FORK.md`, translates the game's MIPS code
  to C++, compiles it to WebAssembly and supplies the PS2 runtime (kernel, IOP,
  GS on WebGPU, SPU2, VU).
- `web/` is our page: disc picker, browser-side cache, gamepad handling.
- `tools/` are our scripts: ISO check, port wrapper, log scanner, build, deploy.
- `docs/re/` holds reverse-engineering notes as we decompile.

Design: `docs/superpowers/specs/2026-10-10-burnout3-browser-port-design.md`.
Credits for every project we learned from or built on: `docs/research/PROVENANCE.md`.

## Build (WSL2 Ubuntu)
```
bash tools/wsl-setup.sh          # once
python3 tools/verify-iso.py ~/b3/iso/burnout3.iso
tools/port.sh                    # recompile + headless exploring runs
tools/build-web.sh               # page into ~/b3/out/web
python3 ~/b3/ps2-web-recomp/scripts/serve.py ~/b3/out/web ~/b3/iso/burnout3.iso
```
Tests: `python3 -m unittest discover -s tests -p 'test_*.py'` and `node --test tests/`.

## Controls
Standard-mapping gamepads (DualSense, DualShock 4, Xbox) work through the
browser. Keyboard: Z cross, X circle, A square, S triangle, Q/W L1/R1, 1/2
L2/R2, Enter start, Backspace select, arrows D-pad, IJKL left stick, F fullscreen.

## License
MIT for our code. Adapted files carry their origin in a header comment.
```

- [ ] **Step 7: Commit and update progress**

Add a milestone 3 row to `docs/PROGRESS.md` (URL redacted to the project name, protection method, wasm size).
```bash
cd "$REPO" && git status --short
git add README.md tools/deploy/vercel.json tools/deploy/deploy.sh docs/PROGRESS.md && git commit -m "deploy: private Vercel static deploy with isolation headers; README"
```

---

## Self-review notes

- Spec coverage: architecture (Tasks 2, 7), disc flow steps 1–6 (Tasks 10–12), gamepad (Task 8), deployment and privacy (Task 13), milestones 0–3 (Tasks 1–13), verification (Tasks 4–6, 9, 12), credits (Tasks 2, 7). Milestone 4 is deliberately out of this plan.
- Deviation from the spec, recorded there too: the fork is pinned in `external/FORK.md`, not a submodule, and no `.gitignore` exists.
- Review Focus items map to: 1 → Task 11 interrupted-copy test and Task 12 check 4; 2 → Task 3 and Task 10 tests, Task 12 check 1; 3 → Task 8 tests and check; 4 → Task 11 quota test and Task 12 flow; 5 → Task 4 unknown-tag test.
- Names used consistently: `B3Disc.identify/cachedDisc/cacheDisc/ejectDisc`, `B3Pad.applyDeadZone/toPs2Axis`, `Module.discFile/discSize`, `PS2_WEB_SHELL`, `tools/port.sh`, `tools/build-web.sh`, `tools/scan-log.py`, `tools/verify-iso.py`.
