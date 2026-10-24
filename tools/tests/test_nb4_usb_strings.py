# SPDX-License-Identifier: GPL-2.0-only
"""Run the production USB encoder with real buffer limits and guard bytes."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools.tests.test_stm32_usart_tx_mode import function

ROOT = pathlib.Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('c++'), 'no host C++ compiler available')
class UsbStrings(unittest.TestCase):
    def test_complete_identity_and_long_strings_cannot_overrun_the_descriptor(self):
        core = ROOT / 'radio/src/thirdparty/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c'
        encoder = function(core.read_text(), 'void USBD_GetString(')
        config = ROOT / 'radio/src/targets/common/arm/stm32/usbd_conf.h'
        setup = r'''
#include <cassert>
#include <cstring>
#include "usbd_conf.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define USB_DESC_TYPE_STRING 3U
'''
        scenario = r'''
struct Guarded { unsigned char before[8], data[USBD_MAX_STR_DESC_SIZ], after[8]; };
int main() {
 Guarded buffer; memset(&buffer,0xa5,sizeof(buffer)); unsigned short length=0;
 char identity[]="APXINFO1;nb4plus;apextx-nb4-2.12.5-apextx-0.1.0-alpha.4-dev (6ea767d1)";
 USBD_GetString((unsigned char*)identity,buffer.data,&length);
#if defined(RADIO_NB4)
 assert(length==2+2*strlen(identity));
 for(unsigned i=0;i<strlen(identity);++i) assert(buffer.data[2+i*2]==identity[i]);
#else
 assert(length==98);
#endif
 char long_text[400]; memset(long_text,'x',sizeof(long_text)); long_text[399]=0;
 USBD_GetString((unsigned char*)long_text,buffer.data,&length);
 assert(length==USBD_MAX_STR_DESC_SIZ && buffer.data[0]==length);
 assert(buffer.data[1]==USB_DESC_TYPE_STRING && length<=254 && length%2==0);
 for(unsigned i=0;i<8;++i) assert(buffer.before[i]==0xa5 && buffer.after[i]==0xa5);
 char short_text[]="DFU Interface";
 USBD_GetString((unsigned char*)short_text,buffer.data,&length);
 assert(length==2+2*strlen(short_text));
 USBD_GetString(nullptr,buffer.data,&length); assert(length==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            for name in ('stm32_cmsis.h', 'stm32_hal.h'):
                (folder / name).write_text('')
            source = folder / 'strings.cpp'
            source.write_text(setup + encoder + scenario)
            for flags in ([], ['-DRADIO_NB4'], ['-DRADIO_NB4', '-DBOOT']):
                with self.subTest(flags=flags):
                    binary = folder / 'strings'
                    result = subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', *flags, '-I', str(folder), '-I', str(config.parent),
                        str(source), '-o', str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    subprocess.run([str(binary)], check=True)
