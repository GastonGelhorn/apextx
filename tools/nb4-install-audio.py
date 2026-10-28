#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Install one ApexTX audio pack without discarding personal recordings."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from nb4_audio_install import *  # Keep the existing CLI/import entry point.

if __name__ == '__main__':
    main()
