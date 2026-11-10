# Arduino-ESP32 3.x splits the core into libraries (FS, Network, WiFi, USB, ...).
# PlatformIO compiles each one alone and does not add the sibling src folders
# to the include path, so LittleFS cannot see FS.h and WiFi cannot see Network.h.
Import("env")

import os

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
libraries_dir = os.path.join(framework_dir, "libraries")
src_dirs = []
for name in sorted(os.listdir(libraries_dir)):
    src = os.path.join(libraries_dir, name, "src")
    if os.path.isdir(src):
        src_dirs.append(src)
if src_dirs:
    env.Prepend(CPPPATH=src_dirs)
