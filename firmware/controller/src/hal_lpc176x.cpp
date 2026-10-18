// =============================================================================
//  HAL: LPC1769 (BTT SKR V1.4 Turbo)
// =============================================================================
#if defined(TARGET_LPC1768) || defined(BOARD_SKR14_TURBO)
#include "hal.h"

#include <usb/usb.h>
#include <usb/usbcfg.h>
#include <usb/usbhw.h>
#include <usb/usbcore.h>
#include <usb/cdc.h>
#include <usb/cdcuser.h>
#include <CDCSerial.h>

extern "C" {
  #include <chanfs/diskio.h>
  #include <lpc17xx_iap.h>
}
#include <string.h>

static LPC_GPIO_TypeDef *const kGpio[5] = { LPC_GPIO0, LPC_GPIO1, LPC_GPIO2, LPC_GPIO3, LPC_GPIO4 };

void FastPin::beginOutput(pin_id_t pin, bool level) {
  pin_ = pin;
  gpio_ = kGpio[LPC176x::pin_port(pin)];
  mask_ = 1UL << LPC176x::pin_bit(pin);
  write(level);
  pinMode(pin, OUTPUT);
  write(level);
}

void FastPin::beginInput(pin_id_t pin, bool pullup) {
  pin_ = pin;
  gpio_ = kGpio[LPC176x::pin_port(pin)];
  mask_ = 1UL << LPC176x::pin_bit(pin);
  pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
}

void FastPin::setOutput() {
  CriticalSection cs;
  gpio_->FIODIR |= mask_;
}

void FastPin::setInput() {
  CriticalSection cs;
  gpio_->FIODIR &= ~mask_;
}

// FIOSET/FIOCLR ir atomāri jau paši par sevi.
void FastPin::writeAtomic(bool v) { write(v); }

namespace hal {

void init() {
  // Iebūvētais USB (CDC seriālais ports). Tāpat kā Marlin: atvienojam,
  // dodam OS laiku to pamanīt, tad pieslēdzamies no jauna.
  USB_Init();
  USB_Connect(false);
  delay(1000);
  USB_Connect(true);
}

void startStepTimer(uint32_t hz) {
  LPC_SC->PCONP |= (1UL << 1);                                  // TIMER0 barošana
  LPC_SC->PCLKSEL0 = (LPC_SC->PCLKSEL0 & ~(3UL << 2)) | (1UL << 2);  // PCLK = CCLK
  LPC_TIM0->TCR = 2;                                            // reset
  LPC_TIM0->PR  = 0;
  LPC_TIM0->MR0 = SystemCoreClock / hz - 1;
  LPC_TIM0->MCR = 3;                                            // IRQ + reset uz MR0
  LPC_TIM0->IR  = 0x3F;
  NVIC_SetPriority(TIMER0_IRQn, NVIC_EncodePriority(0, 1, 0));  // aiz SysTick, pirms USB/UART
  NVIC_EnableIRQ(TIMER0_IRQn);
  LPC_TIM0->TCR = 1;
}

void idle() {}

// Iestatījumi: pēdējais 32 KB flash sektors (29, 0x78000). Linkera skripts to
// jau atstāj brīvu ("EEPROM emulation"). Ieraksts = sektora dzēšana + 512 B.
static constexpr uint32_t kStoreAddr = 0x78000, kStoreSector = 29, kStoreBlock = 512;

void storageRead(void *buf, uint16_t len) {
  if (len > kStoreBlock) len = kStoreBlock;
  memcpy(buf, reinterpret_cast<const void *>(kStoreAddr), len);
}

bool storageWrite(const void *buf, uint16_t len) {
  if (len > kStoreBlock) return false;
  static uint8_t block[kStoreBlock] __attribute__((aligned(4)));
  memset(block, 0xFF, sizeof block);
  memcpy(block, buf, len);
  IAP_STATUS_CODE s;
  {
    // Flash darbību laikā vektoru tabula (flash) nav pieejama -> bez pārtraukumiem.
    CriticalSection cs;
    s = EraseSector(kStoreSector, kStoreSector);
    if (s == CMD_SUCCESS) s = CopyRAM2Flash(reinterpret_cast<uint8_t *>(kStoreAddr), block, IAP_WRITE_512);
  }
  return s == CMD_SUCCESS;
}

// Cortex-M3 DWT cikla skaitītājs (tieši reģistri, neatkarīgi no CMSIS versijas)
#define REG32(a) (*(volatile uint32_t *)(a))
static constexpr uint32_t kDEMCR = 0xE000EDFC, kDWT_CTRL = 0xE0001000, kDWT_CYCCNT = 0xE0001004;

void cycleCounterInit() {
  REG32(kDEMCR) |= (1UL << 24);    // TRCENA
  REG32(kDWT_CYCCNT) = 0;
  REG32(kDWT_CTRL) |= 1UL;         // CYCCNTENA
}
uint32_t cycles() { return REG32(kDWT_CYCCNT); }
uint32_t cyclesPerSecond() { return SystemCoreClock; }

}  // namespace hal

extern "C" void TIMER0_IRQHandler(void) {
  LPC_TIM0->IR = 1;
  stepper_isr();
}

// -----------------------------------------------------------------------------
// Ietvara USB steks ir CDC + masu atmiņa (SD karte). SD karti neizmantojam, tāpēc
// diska funkcijas vienmēr saka "nav nesēja" (aizstāj vājās mmc_ssp versijas).
// -----------------------------------------------------------------------------
extern "C" {
DSTATUS disk_initialize(BYTE) { return STA_NOINIT | STA_NODISK; }
DSTATUS disk_status(BYTE) { return STA_NOINIT | STA_NODISK; }
DRESULT disk_read(BYTE, BYTE *, DWORD, UINT) { return RES_NOTRDY; }
DRESULT disk_write(BYTE, const BYTE *, DWORD, UINT) { return RES_NOTRDY; }
DRESULT disk_ioctl(BYTE, BYTE, void *) { return RES_NOTRDY; }
}

#endif
