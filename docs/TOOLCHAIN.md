# DJGPP toolchain

Quartz is cross-compiled with **DJGPP** (GCC targeting 32-bit protected-mode
DOS). The compiler runs on Linux and emits DOS executables that use a DPMI host
(CWSDPMI) at runtime.

## Why DJGPP

- Flat 32-bit address space and a hosted libc — direct, C-friendly access to the
  VESA linear framebuffer.
- `Watt-32` (the DJGPP TCP/IP stack) gives us networking in the same toolchain.
- Large body of reference code for VBE/DPMI graphics.

## Building it here (from source)

The prebuilt DJGPP binaries are published only as GitHub release assets, which
this sandbox's proxy blocks. We therefore build from source with the
`andrewwutw/build-djgpp` script, which pulls binutils/GCC from
`mirrorservice.org` (gnu.org mirror) and the DJGPP runtime from delorie.com —
all reachable.

```bash
cd /home/user/andrewwutw/build-djgpp
export DJGPP_PREFIX=/opt/djgpp
export ENABLE_LANGUAGES=c,c++
export MAKE="make -j4"
./build-djgpp.sh 12.2.0        # ~20-40 min; installs i586-pc-msdosdjgpp-gcc
```

Host deps (already installed): `flex texinfo gperf zlib1g-dev bison make gcc`.

Result: `/opt/djgpp/bin/i586-pc-msdosdjgpp-gcc`. The Quartz `Makefile` points at
`$(DJGPP_PREFIX)` (default `/opt/djgpp`).

> **Sandbox note:** running the build script is gated by the auto-permission
> classifier ("Code from External") because it executes third-party
> `configure`/`make`. It must be explicitly allowed (a Bash permission rule, a
> more permissive permission mode, or interactive approval) before it will run.
