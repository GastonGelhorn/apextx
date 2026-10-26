#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Execute a built NB4 ROM-return entry with CCM initially inaccessible.

Requires unicorn==2.1.4. This checks the actual linked Thumb code, including
compiler prologues and flash-resident callees, rather than a host C++ model.
"""
import argparse
import struct
from pathlib import Path

from nb4_rom_flash import BOOT_ADDRESS, automatic_start_address, validate_full_image


def verify(image):
    from unicorn import (Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS,
                         UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE)
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_PRIMASK

    validate_full_image(image)
    vector = automatic_start_address(image)
    if vector is None:
        raise ValueError('the image has no ROM-return entry')
    stack, reset = struct.unpack_from('<II', image, vector - BOOT_ADDRESS)
    ahb1enr, aircr = 0x40023830, 0xE000ED0C
    for initial_clocks in (0, 1):
        cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        cpu.mem_map(BOOT_ADDRESS, len(image))
        cpu.mem_write(BOOT_ADDRESS, image)
        cpu.mem_map(0x20000000, 0x30000)
        cpu.mem_map(0x10000000, 0x10000)
        cpu.mem_map(0x40023000, 0x1000)
        cpu.mem_map(0xE000E000, 0x1000)
        cpu.mem_write(ahb1enr, struct.pack('<I', initial_clocks))
        enabled, reset_requested = False, False

        def write(uc, _access, address, size, value, _data):
            nonlocal enabled, reset_requested
            if address == ahb1enr and value & (1 << 20) and not enabled:
                if not uc.reg_read(UC_ARM_REG_PRIMASK):
                    raise ValueError('interrupts were not masked before enabling CCM')
                enabled = True
            if address == aircr and value >> 16 == 0x5FA and value & 4:
                reset_requested = True
                uc.emu_stop()

        def ccm_access(_uc, _access, _address, _size, _value, _data):
            if not enabled:
                raise ValueError('ROM-return code accessed CCM before enabling its clock')

        cpu.hook_add(UC_HOOK_MEM_WRITE, write)
        cpu.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, ccm_access,
                     begin=0x10000000, end=0x1000FFFF)
        cpu.reg_write(UC_ARM_REG_SP, stack)
        try:
            cpu.emu_start(reset, BOOT_ADDRESS + len(image), count=1000)
        except UcError as error:
            raise ValueError(f'ROM-return code accessed unavailable memory or faulted: {error}') from error
        if not enabled or not reset_requested:
            raise ValueError('ROM-return entry did not enable CCM and request a real MCU reset')
        command = struct.unpack('<I', cpu.mem_read(0x1000FFFC, 4))[0]
        clocks = struct.unpack('<I', cpu.mem_read(ahb1enr, 4))[0]
        if command != 0x52534D45 or clocks != initial_clocks | (1 << 20):
            raise ValueError('resume request or unrelated clock bits were not preserved')
    return vector


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    try:
        vector = verify(args.image.read_bytes())
    except (ValueError, RuntimeError) as error:
        parser.exit(1, f'ROM automatic-start verification failed: {error}\n')
    print(f'ROM automatic-start code verified at {vector:#x}: CCM enabled before stack access, '
          'one-shot resume stored, real MCU reset requested.')


if __name__ == '__main__':
    main()
