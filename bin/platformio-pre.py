#!/usr/bin/env python3
# trunk-ignore-all(ruff/F821)
# trunk-ignore-all(flake8/F821): For SConstruct imports
Import("env")
platform = env.PioPlatform()

if platform.name == "native":
    env.Replace(PROGNAME="meshtasticd")
else:
    from readprops import readProps
    prefsLoc = env["PROJECT_DIR"] + "/version.properties"
    verObj = readProps(prefsLoc)
    # DL9SAU fork: filename uses long_fork (with -DL9SAU.g<hash> marker), embedded
    # APP_VERSION uses long (upstream-vanilla, Protobuf-safe). See readprops.py.
    env.Replace(PROGNAME=f"firmware-{env.get('PIOENV')}-{verObj['long_fork']}")
    env.Replace(ESP32_FS_IMAGE_NAME=f"littlefs-{env.get('PIOENV')}-{verObj['long_fork']}")

# Print the new program name for verification
print(f"PROGNAME: {env.get('PROGNAME')}")
if platform.name == "espressif32":
    print(f"ESP32_FS_IMAGE_NAME: {env.get('ESP32_FS_IMAGE_NAME')}")
