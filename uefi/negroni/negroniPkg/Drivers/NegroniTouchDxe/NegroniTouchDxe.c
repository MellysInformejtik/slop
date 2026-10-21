/** @file
  negroni touch bring-up: QUP0 SE4 clocks, I2C pins, S3908 reset, a TouchComm
  probe. Everything stays on at ExitBootServices, so an OS driver without a
  Qualcomm clock/GPIO stack (Windows NegroniTouch) only drives the GENI I2C.

  Results go to the screen log (DEBUG): "NTOUCH: ..." lines.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/EFIClock.h>

#include "tlmm.h"
#include "geni_i2c.h"
#include "syna_tcm.h"

#define SE4_BASE   0x00990000UL
#define S3908_ADDR 0x4b

STATIC struct geni_i2c mI2c = { (volatile uint8_t *)SE4_BASE, 0, 0 };
STATIC struct tcm      mTcm;

STATIC CONST CHAR8 *mClocks[] = {
  "gcc_qupv3_wrap_0_m_ahb_clk",
  "gcc_qupv3_wrap_0_s_ahb_clk",
  "gcc_qupv3_wrap0_core_clk",
  "gcc_qupv3_wrap0_core_2x_clk",
  "gcc_qupv3_wrap0_s4_clk",
};

STATIC int I2cRead (void *Ctx, uint8_t *Buf, uint32_t Len)   { return geni_i2c_read (Ctx, S3908_ADDR, Buf, Len); }
STATIC int I2cWrite (void *Ctx, const uint8_t *Buf, uint32_t Len) { return geni_i2c_write (Ctx, S3908_ADDR, Buf, Len); }
STATIC void Wait1ms (void) { gBS->Stall (1000); }

STATIC
VOID
EnableClocks (
  VOID
  )
{
  EFI_CLOCK_PROTOCOL  *Clock;
  EFI_STATUS          Status;
  UINTN               Id;
  UINTN               Index;

  Status = gBS->LocateProtocol (&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "NTOUCH: no clock protocol %r\n", Status));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mClocks); Index++) {
    Status = Clock->GetClockID (Clock, mClocks[Index], &Id);
    if (!EFI_ERROR (Status)) {
      Status = Clock->EnableClock (Clock, Id);
    }

    DEBUG ((DEBUG_ERROR, "NTOUCH: %a %r\n", mClocks[Index], Status));
  }

  // 19.2 MHz SE clock: the 400 kHz divider table assumes it
  if (!EFI_ERROR (Clock->GetClockID (Clock, "gcc_qupv3_wrap0_s4_clk", &Id))) {
    UINT32 Hz = 0;
    Status = Clock->SetClockFreqHz (Clock, Id, 19200000, EFI_CLOCK_FREQUENCY_HZ_CLOSEST, &Hz);
    DEBUG ((DEBUG_ERROR, "NTOUCH: s4 clock %u Hz %r\n", Hz, Status));
  }
}

EFI_STATUS
EFIAPI
NegroniTouchDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  volatile UINT8  *Tlmm = (volatile UINT8 *)(UINTN)TLMM_BASE;
  int             Rc;
  UINTN           Try;

  EnableClocks ();
  nt_pins_setup (Tlmm);
  gBS->Stall (10 * 1000);
  tlmm_out (Tlmm, NT_PIN_RESET, 1);   // release reset
  gBS->Stall (200 * 1000);            // firmware boot

  Rc = geni_i2c_init (&mI2c);
  DEBUG ((DEBUG_ERROR, "NTOUCH: SE4 proto %u fifo %u init %d\n", geni_i2c_proto (&mI2c), mI2c.tx_depth, Rc));
  if (Rc != GENI_I2C_OK) {
    return EFI_SUCCESS;
  }

  {
    uint8_t  Raw[4] = { 0 };
    int      R      = geni_i2c_read (&mI2c, S3908_ADDR, Raw, 4);

    DEBUG ((DEBUG_ERROR, "NTOUCH: raw read %d (-2 nack -3 bus -4 timeout) %02x %02x %02x %02x\n", R, Raw[0], Raw[1], Raw[2], Raw[3]));
    DEBUG ((DEBUG_ERROR, "NTOUCH: geni status 0x%x m_irq 0x%x reset pin %d 1v8 pin %d\n",
            geni_rd (&mI2c, SE_GENI_STATUS), geni_rd (&mI2c, SE_GENI_M_IRQ_STATUS),
            (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_RESET)) >> 1) & 1),
            (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_1V8_EN)) >> 1) & 1)));
  }

  mTcm.io.ctx   = &mI2c;
  mTcm.io.read  = I2cRead;
  mTcm.io.write = I2cWrite;

  // After reset the controller reports IDENTIFY (0x10); ATTN goes low while a message waits.
  for (Try = 0; Try < 50; Try++) {
    Rc = tcm_read_message (&mTcm);
    if (Rc <= 0) {
      break;
    }

    gBS->Stall (10 * 1000);
  }

  DEBUG ((DEBUG_ERROR, "NTOUCH: read %d code 0x%x len %u attn %d\n", Rc, mTcm.code, mTcm.payload_len,
          tlmm_in (Tlmm, NT_PIN_IRQ)));
  if (Rc == 0 && mTcm.code == TCM_REPORT_IDENTIFY && mTcm.payload_len >= 2) {
    DEBUG ((DEBUG_ERROR, "NTOUCH: identify version %u mode %u\n", mTcm.payload[0], mTcm.payload[1]));
  }

  Rc = tcm_get_report_config (&mTcm, Wait1ms);
  DEBUG ((DEBUG_ERROR, "NTOUCH: report config %d, %u bytes\n", Rc, mTcm.config_len));
  DEBUG ((DEBUG_ERROR, "NTOUCH: attn now %d (0 = message waiting)\n", tlmm_in (Tlmm, NT_PIN_IRQ)));
  return EFI_SUCCESS;
}
