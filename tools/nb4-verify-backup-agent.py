#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the read-only RAM agent's link map and image before packaging it."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from nb4_backup_agent import RAM, RAM_CAPACITY, validate_agent


def verify_crt_returns(image, addresses):
    # Arm GNU 14.2.Rel1 supplies these two halves in separate input objects.
    # A reference to _init keeps crti.o alive, but not crtn.o under section GC.
    # Check the linked ABI sequence, not just the presence of its entry symbol.
    sequence = bytes.fromhex('f8b500bff8bc08bc9e467047')
    for name in ('_init', '_fini'):
        at = addresses.get(name, 0) - RAM
        if at < 0 or image[at:at + len(sequence)] != sequence:
            raise ValueError(f'The agent {name} startup/return sequence is incomplete.')


def verify_hal_time_base(image, addresses, timer):
    if '<HAL_IncTick>' in timer:
        return
    # The qualified compiler can inline HAL_IncTick into the interrupt. Its
    # literal must name HAL's uwTick, never a second private millisecond clock.
    at = addresses.get('SysTick_Handler', 0) - RAM
    sequence = bytes.fromhex('024a136801331360704700bf')
    if (at < 0 or image[at:at + 12] != sequence or
            image[at + 12:at + 16] != struct.pack('<I', addresses.get('uwTick', 0))):
        raise ValueError('The agent timer must advance the USB driver HAL time base.')


def verify(elf, binary, nm, *, restore=False):
    data, image = Path(elf).read_bytes(), Path(binary).read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected a little-endian 32-bit ARM ELF.')
    _, _, machine, _, entry, offset, _, _, _, record_size, count, *_ = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if machine != 40 or record_size != 32 or not 1 <= count <= 32 or offset + count * 32 > len(data):
        raise ValueError('Invalid agent program headers.')
    loads = []
    for index in range(count):
        kind, source, address, physical, size, memory, _, _ = struct.unpack_from('<8I', data, offset + index * 32)
        if kind != 1:
            continue
        if (size > memory or source + size > len(data) or address != physical or
                not RAM <= address < RAM + RAM_CAPACITY or memory > RAM + RAM_CAPACITY - address):
            raise ValueError('A backup-agent load segment is outside reserved SRAM.')
        loads.append(dict(address=address, bytes=size, memory=memory))
    if not loads or min(v['address'] for v in loads) != RAM:
        raise ValueError('The backup agent has no SRAM vector table.')
    digest = hashlib.sha256(image).hexdigest()
    validate_agent(image, digest)
    if struct.unpack_from('<I', image, 4)[0] != entry:
        raise ValueError('The backup-agent entry and vector do not agree.')
    at = (entry & ~1) - RAM
    if image[at:at+2] != b'\x72\xb6':
        raise ValueError('The backup agent must mask ROM interrupts before initialization.')
    symbols = subprocess.check_output([nm, '-C', str(elf)], text=True)
    objdump = re.sub(r'nm(\.exe)?$', r'objdump\1', nm)
    startup = subprocess.check_output([objdump, '-d', '--disassemble=Reset_Handler', str(elf)], text=True)
    early, clock = startup.find('<nb4BackupAgentEarlyInit>'), startup.find('<SystemClock_Config>')
    if early < 0 or clock < 0 or early >= clock:
        raise ValueError('The RAM agent must latch power and normalize ROM clocks before configuring its PLL.')
    if 'IPSR' not in startup or '<AgentThreadStart>' not in startup or '<AgentUnsupportedContext>' not in startup:
        raise ValueError('The agent must validate its ROM exception context before starting.')
    addresses = {name:int(address,16) for address,name in re.findall(
        r'^([0-9a-fA-F]+)\s+\w\s+(\S+)$',symbols,re.M)}
    verify_crt_returns(image, addresses)
    timer = subprocess.check_output([objdump, '-d', '--disassemble=SysTick_Handler', str(elf)], text=True)
    verify_hal_time_base(image, addresses, timer)
    for name in ('HAL_PCD_MspInit', 'HAL_PCD_SetupStageCallback', 'HAL_PCD_DataInStageCallback',
                 'HAL_PCD_DataOutStageCallback', 'HAL_PCD_ResetCallback'):
        at = addresses.get(name, 0) - RAM
        if at < 0 or image[at:at + 2] in (b'', b'\x70\x47'):
            raise ValueError(f'The agent USB callback {name} was removed or replaced by a no-op.')
    for name in ('nb4BackupEntryIpsr','nb4BackupEntryScr'):
        at = addresses.get(name,0)
        if not RAM <= at < RAM+RAM_CAPACITY or addresses['_sbss'] <= at < addresses['_ebss']:
            raise ValueError('Entry diagnostics must survive startup BSS initialization.')
    if re.search(r'\b(HAL_FLASH_Program|HAL_FLASHEx_Erase|flashSpiWrite|flashSpiErase|f_mkfs)\b', symbols):
        raise ValueError('A storage programming driver was linked into the read-only backup agent.')
    if (b'APXNOR1\0' if restore else b'APXBKP3\0') not in image:
        raise ValueError('The backup agent must include the bounded-lifetime protocol.')
    if not restore and 'NorRestoreCore' in symbols:
        raise ValueError('The external-storage writer must not enter the read-only agent.')
    return dict(format='apextx-nor-restore-agent-1' if restore else 'apextx-backup-agent-1', address=RAM, bytes=len(image), sha256=digest,
                entry=entry, read_only=not restore, flash_write_driver=False, nor_write=restore, segments=loads,
                agent_protocol=4 if restore else 3, power_key_exit=True, idle_timeout_ms=120000,
                complete_crt_returns=True,
                shared_hal_time_base=True,
                usb_callbacks_retained=True,
                exception_context_guard=True,
                hardware_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--bin', type=Path, required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--restore', action='store_true', help='Verify the separate experimental external-NOR restore agent.')
    args = parser.parse_args()
    try:
        result = verify(args.elf, args.bin, args.nm, restore=args.restore)
        content = json.dumps(result, indent=2) + '\n'
        if args.report: args.report.write_text(content)
        print(content, end='')
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f'Error: {error}\n')
