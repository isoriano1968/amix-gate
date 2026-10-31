"""xbuild.py -- cross-build Mesa 18.3's OSMesa (gallium, softpipe) as one
static library for a target the meson build does not know: AMIX (m68k) or
bare-metal ARM (Z3660 core 1).

The source list, include paths and generated files come from the host meson
build in src/host-osmesa (compile_commands.json); host-only defines are
dropped and the target's own are added.

    python3 -I build/xbuild.py amix|arm [--clean] [-j N]
"""
import json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HOME = os.path.expanduser("~")
TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = os.path.join(TOP, "src", "host-osmesa")

# the static libraries libOSMesa.so is made of (meson's <lib>.a.p object dirs)
LIBS = ["src/compiler/glsl/glcpp/libglcpp.a", "src/compiler/glsl/libglsl.a", "src/compiler/libcompiler.a",
        "src/compiler/nir/libnir.a", "src/gallium/auxiliary/libgallium.a",
        "src/gallium/drivers/softpipe/libsoftpipe.a", "src/gallium/state_trackers/osmesa/libosmesa_st.a",
        "src/gallium/winsys/sw/null/libws_null.a", "src/mapi/glapi/libglapi_static.a",
        "src/mesa/libmesa_gallium.a", "src/util/libmesa_util.a",
        "src/gallium/targets/osmesa/libOSMesa.so.8.0.0"]

# host facts that are wrong or unavailable on the targets (NDEBUG is set
# per target: the host build may be a debug one)
DROP = {"GLX_USE_TLS", "HAVE_DLADDR", "HAVE_DLFCN_H", "HAVE_DL_ITERATE_PHDR", "HAVE_ENDIAN_H",
        "HAVE_LIBDRM", "HAVE_LINUX_FUTEX_H", "HAVE_MEMFD_CREATE", "HAVE_MKOSTEMP",
        "HAVE_POSIX_MEMALIGN", "HAVE_PTHREAD_SETAFFINITY", "HAVE_TIMESPEC_GET", "HAVE_VALGRIND",
        "HAVE_ZLIB", "ENABLE_SHADER_CACHE", "MAJOR_IN_SYSMACROS", "USE_SSE41", "USE_X86_64_ASM",
        "_FILE_OFFSET_BITS", "HAVE_FUNC_ATTRIBUTE_ALIAS", "HAVE_FUNC_ATTRIBUTE_VISIBILITY",
        "USE_DRICONF"}

# not needed without a window system, network or DRM (hud uses signals)
EXCLUDE = ("hud/hud_context.c", "util/u_network.c", "renderonly/renderonly.c",
           "rtasm/rtasm_execmem.c", ".S")

# per-target, per-file extra flags (gcc 14 m68k: internal compiler error in
# reload at -O2; ARM: see below)
PERFILE = {"amix": {"st_glsl_to_tgsi.cpp": ["-O1"]},
           # the interpreter's 4-lane loops as NEON (needs unsafe math on ARM)
           "arm": {"tgsi_exec.c": ["-O3", "-funsafe-math-optimizations"],
                   # GCC uses NEON for float vectors only with unsafe math
                   "sp_fs_fast.c": ["-O3", "-funsafe-math-optimizations"]}}

COMPAT = os.path.join(TOP, "compat")

TARGETS = {
    "amix": {
        "cc": HOME + "/opt/m68k-amix/bin/m68k-elf-gcc",
        "cxx": HOME + "/opt/m68k-amix/bin/m68k-elf-g++",
        "ar": HOME + "/opt/m68k-amix/bin/m68k-elf-ar",
        "flags": ["-O2", "-DNDEBUG", "-m68060", "-fno-strict-aliasing", "-fcommon", "-w", "-U__linux__", "-Ulinux",
                  "-D__amix__", "-DSVR4", "-DHAVE_ENDIAN_H",
                  "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
                  "-Wno-error=incompatible-pointer-types", "-fpermissive",
                  # int32_t as int, as Mesa assumes everywhere (m68k-elf says long)
                  "-U__INT32_TYPE__", "-D__INT32_TYPE__=int", "-U__UINT32_TYPE__",
                  "-D__UINT32_TYPE__=unsigned int", "-U__INT_LEAST32_TYPE__", "-D__INT_LEAST32_TYPE__=int",
                  "-U__UINT_LEAST32_TYPE__", "-D__UINT_LEAST32_TYPE__=unsigned int", "-fno-builtin-sin", "-fno-builtin-cos",
                  "-I" + COMPAT + "/amix", "-include", COMPAT + "/amix/amixgl_compat.h"],
        "cflags": ["-std=gnu99"],
        "cxxflags": ["-std=gnu++11"],
    },
    "arm": {
        "cc": HOME + "/opt/arm-none-eabi/bin/arm-none-eabi-gcc",
        "cxx": HOME + "/opt/arm-none-eabi/bin/arm-none-eabi-g++",
        "ar": HOME + "/opt/arm-none-eabi/bin/arm-none-eabi-ar",
        "flags": ["-O2", "-DNDEBUG", "-mcpu=cortex-a9", "-marm", "-mfpu=neon", "-fno-short-enums", "-mfloat-abi=hard", "-ffunction-sections",
                  "-fdata-sections", "-w", "-D__z3660core__", "-DHAVE_ENDIAN_H",
                  "-Wno-error=implicit-function-declaration", "-Wno-error=int-conversion",
                  "-Wno-error=incompatible-pointer-types", "-fpermissive", "-U__INT32_TYPE__", "-D__INT32_TYPE__=int", "-U__UINT32_TYPE__",
                  "-D__UINT32_TYPE__=unsigned int", "-I" + COMPAT + "/arm",
                  "-include", COMPAT + "/arm/armgl_compat.h"],
        "cflags": ["-std=gnu99"],
        "cxxflags": ["-std=gnu++11"],
    },
}


def entries():
    cc = json.load(open(os.path.join(HOST, "compile_commands.json")))
    dirs = tuple(l + ".p/" for l in LIBS)
    return [e for e in cc if e["output"].startswith(dirs) and not e["file"].endswith(EXCLUDE)]


def command(t, e, out):
    args = shlex.split(e["command"])
    cxx = e["file"].endswith((".cpp", ".cc"))
    keep = []
    for a in args[1:]:
        if a.startswith("-I") or a.startswith("-isystem"):
            keep.append(a)
        elif a.startswith("-D"):
            name = re.split(r"[=]", a[2:], maxsplit=1)[0]
            if name not in DROP:
                keep.append(a)
    extra = PERFILE.get(t["name"], {}).get(os.path.basename(e["file"]), [])
    return ([t["cxx"] if cxx else t["cc"]] + t["flags"] + (t["cxxflags"] if cxx else t["cflags"])
            + keep + extra + ["-c", e["file"], "-o", out])


def main():
    target = sys.argv[1]
    t = TARGETS[target]
    t["name"] = target
    jobs = int(sys.argv[sys.argv.index("-j") + 1]) if "-j" in sys.argv else os.cpu_count()
    out = os.path.join(TOP, "build", target)
    if "--clean" in sys.argv:
        subprocess.run(["rm", "-rf", out])
    os.makedirs(os.path.join(out, "obj"), exist_ok=True)
    log = os.path.join(out, "errors.log")
    es = entries()

    def stale(o, src):
        """the object is older than its source or any header it was built
        with (gcc -MMD's list); without a list: rebuild"""
        if not os.path.exists(o) or not os.path.exists(o + ".d"):
            return True
        t = os.path.getmtime(o)
        deps = open(o + ".d").read().replace("\\\n", " ").split(":", 1)[-1].split()
        return any(not os.path.exists(os.path.join(HOST, d)) or os.path.getmtime(os.path.join(HOST, d)) > t
                   for d in deps + [src])

    def one(e):
        o = os.path.join(out, "obj", e["output"].replace("/", "_"))
        src = os.path.join(HOST, e["file"])
        if not stale(o, src):
            return o, None
        cmd = command(t, e, o) + ["-MMD", "-MF", o + ".d"]
        r = subprocess.run(cmd, cwd=HOST, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        return o, (None if r.returncode == 0 else "$ " + " ".join(cmd) + "\n" + r.stdout)

    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(one, es))
    bad = [(o, m) for o, m in res if m]
    with open(log, "w") as f:
        for o, m in bad:
            f.write(m + "\n")
    print("%s: %d objects, %d failed%s" % (target, len(res), len(bad), " (see %s)" % log if bad else ""))
    for o, m in bad[:15]:
        print("  ", os.path.basename(o))
    if bad:
        sys.exit(1)
    lib = os.path.join(out, "libOSMesa.a")
    if os.path.exists(lib):
        os.remove(lib)
    subprocess.run([t["ar"], "rcs", lib] + [o for o, _ in res], check=True)
    print("->", lib)


main()
