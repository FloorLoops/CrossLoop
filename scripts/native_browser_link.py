"""Enable browser archive LTO after the framework installs its linker flags."""
Import("env", "projenv")

if env.subst("$PIOENV") != "x4pro-native-probe":
    raise RuntimeError("Native browser linking is restricted to its local X4 environment")

# The packaged SDK adds -fno-lto after pre-scripts. Its precompiled libraries
# remain unchanged. Browser C archives contain LTO input; the optional personal
# profile below also enables it for project objects in src/.
flags = list(env["LINKFLAGS"])
if "-fno-lto" not in flags:
    raise RuntimeError("Expected the pinned SDK's explicit no-LTO linker flag")
env.Replace(LINKFLAGS=[flag for flag in flags if flag != "-fno-lto"])
env.Append(LINKFLAGS=["-flto=2", "-fuse-linker-plugin", "-Oz"])

# PlatformIO has already cloned the project and library environments at this
# point. Modify projenv explicitly; library and SDK compile flags stay intact.
if env.GetProjectOption("custom_crossloop_project_lto", "no") == "yes":
    for name in ("CCFLAGS", "CFLAGS", "CXXFLAGS"):
        projenv.Replace(**{name: [flag for flag in projenv.get(name, []) if flag != "-fno-lto"]})
    projenv.Append(CCFLAGS=["-flto=2", "-Oz"])
