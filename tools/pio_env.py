"""PlatformIO pre-script: reproduce the ESP-IDF build's board/sdkconfig setup.

The ESP-IDF path (winbuild.py) passes two things that PlatformIO's espidf
builder does not: the FLIPPER_BOARD environment variable and a two-file
SDKCONFIG_DEFAULTS chain. Without the chain ESP-IDF would also load
sdkconfig.defaults.esp32s3, which turns on PSRAM and 16 MB flash - fatal on a
Cardputer (no PSRAM, 8 MB flash).

`tools/pio_env.py` runs in the SCons process before the CMake build starts, so
setting os.environ here is visible to the CMake/IDF subprocesses.
"""

import os
import pathlib

Import("env")  # noqa: F821 - injected by PlatformIO

BOARD = "m5stack_cardputer_adv_cap"
PROJECT_DIR = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821

os.environ["FLIPPER_BOARD"] = BOARD

defaults = [PROJECT_DIR / "sdkconfig.defaults"]
board_defaults = PROJECT_DIR / f"sdkconfig.defaults.{BOARD}"
if board_defaults.exists():
    defaults.append(board_defaults)

os.environ["SDKCONFIG_DEFAULTS"] = ";".join(str(p) for p in defaults)

print(f"[pio_env] FLIPPER_BOARD={BOARD}")
print(f"[pio_env] SDKCONFIG_DEFAULTS={os.environ['SDKCONFIG_DEFAULTS']}")
