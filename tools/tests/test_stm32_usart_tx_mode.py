# SPDX-License-Identifier: GPL-2.0-only
"""Exercise the actual USART TX functions against a small register model."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class UsartTxModeTest(unittest.TestCase):
    def test_irq_fallback_then_dma_resumes_requests_and_preserves_busy_frame(self):
        source = (ROOT / 'radio/src/targets/common/arm/stm32/stm32_usart_driver.cpp').read_text()
        actual = '\n'.join(function(source, signature) for signature in (
            'void stm32_usart_enable_tx_irq(',
            'uint8_t stm32_usart_tx_completed(',
            'void stm32_usart_send_buffer(',
        ))
        model = r'''
#include <cstdint>
#include <cassert>
// Host pointers are wider than the MCU's register addresses.
#define uint32_t uintptr_t
struct Uart { bool dma=true, txirq=false, txe=true; };
struct Dma { bool enabled=false; int initializations=0; };
struct stm32_usart_t { Uart* USARTx; Dma* txDMA; int txDMA_Stream=0, txDMA_Channel=0, txDMA_IRQn=0, IRQn=0; };
struct LL_DMA_InitTypeDef { uintptr_t Channel, PeriphOrM2MSrcAddress, Direction,
 MemoryOrM2MDstAddress, MemoryOrM2MDstIncMode, NbData, Priority, FIFOMode, FIFOThreshold; };
#define IS_HALF_DUPLEX(u) false
#define LL_DMA_DIRECTION_MEMORY_TO_PERIPH 1
#define LL_DMA_MEMORY_INCREMENT 1
#define LL_DMA_PRIORITY_VERYHIGH 3
#define LL_DMA_FIFOMODE_ENABLE 1
#define LL_DMA_FIFOTHRESHOLD_FULL 3
bool LL_USART_IsEnabledDMAReq_TX(Uart* u){return u->dma;}
void LL_USART_DisableDMAReq_TX(Uart* u){u->dma=false;}
void LL_USART_EnableDMAReq_TX(Uart* u){u->dma=true;}
void LL_USART_EnableIT_TXE(Uart* u){u->txirq=true;}
bool LL_USART_IsEnabledIT_TXE(Uart* u){return u->txirq;}
bool LL_USART_IsActiveFlag_TXE(Uart* u){return u->txe;}
bool LL_DMA_IsEnabledStream(Dma* d,int){return d->enabled;}
void LL_DMA_EnableStream(Dma* d,int){d->enabled=true;}
void LL_DMA_DeInit(Dma* d,int){d->enabled=false;}
void LL_DMA_Init(Dma* d,int,LL_DMA_InitTypeDef*){++d->initializations;}
void LL_DMA_StructInit(LL_DMA_InitTypeDef* p){*p={};}
void stm32_dma_enable_clock(Dma*){}
uintptr_t LL_USART_DMA_GetRegAddr(Uart*){return 0;}
void LL_DMA_EnableIT_TC(Dma*,int){}
void LL_USART_ClearFlag_TC(Uart*){}
bool NVIC_GetEnableIRQ(int){return true;}
void _enable_usart_irq(const stm32_usart_t*){}
void _half_duplex_output(const stm32_usart_t*){}
void stm32_usart_wait_for_tx_dma(const stm32_usart_t* u){u->txDMA->enabled=false;}
'''
        scenario = r'''
int main(){
 Uart uart; Dma dma; stm32_usart_t port{&uart,&dma}; uint8_t frame[8]{};
 stm32_usart_enable_tx_irq(&port);
 assert(!uart.dma && uart.txirq);
 assert(!stm32_usart_tx_completed(&port));
 // Last byte of the IRQ fallback has been handed to the UART.
 uart.txirq=false;
 assert(stm32_usart_tx_completed(&port));
 stm32_usart_send_buffer(&port,frame,sizeof(frame));
 assert(uart.dma && dma.enabled);
 assert(!stm32_usart_tx_completed(&port));
 int starts=dma.initializations;
 stm32_usart_send_buffer(&port,frame,sizeof(frame));
 assert(dma.initializations==starts);
 dma.enabled=false;
 assert(stm32_usart_tx_completed(&port));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory)
            (path / 'test.cpp').write_text(model + actual + scenario)
            subprocess.run(['c++', '-std=c++17', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True, capture_output=True, text=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
