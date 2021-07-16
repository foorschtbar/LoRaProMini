Import("env")

my_flags = env.ParseFlags(env['BUILD_FLAGS'])
# some flags are just defines without value
# some are key, value pairs
for x in my_flags.get("CPPDEFINES"):
    #print(x)
    if isinstance(x, list):
        # grab as key, value
        k, v = x
        if k == "VERSION_MAJOR":
            version_major = v
        if k == "VERSION_MINOR":
            version_minor= v
            

build_tag = env['PIOENV']
# print(build_tag)

progname = f"firmware_v{version_major}.{version_minor}_{build_tag}"
print(progname)

env.Replace(PROGNAME=progname)