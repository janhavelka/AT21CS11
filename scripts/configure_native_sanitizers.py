"""Select the strongest sanitizer set supported by the native host."""

import os

Import("env")  # type: ignore[name-defined]  # PlatformIO/SCons injects Import.


if os.name == "nt":
    # The supported local MinGW host has no sanitizer runtime libraries. GCC's
    # trap mode still instruments undefined behavior without a runtime link.
    env.AppendUnique(
        CCFLAGS=["-fsanitize=undefined", "-fsanitize-undefined-trap-on-error"]
    )
else:
    sanitizer_flag = "-fsanitize=address,undefined"
    env.AppendUnique(CCFLAGS=[sanitizer_flag])
    env.AppendUnique(LINKFLAGS=[sanitizer_flag])
