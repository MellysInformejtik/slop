/** @file
  negroni touch bring-up: S3908 supplies (RPMh L6C/L3C + 1.8 V switch), QUP0 SE4 clocks, I2C pins,
  reset, a TouchComm probe. Everything stays on at ExitBootServices, so an OS driver without a
  Qualcomm clock/GPIO/RPMh stack (Windows NegroniTouch) only drives the GENI I2C.

  Every step goes to the DEBUG log ("NTOUCH: ...") and to a text log published with
  NEGRONI_TOUCH_LOG_PROTOCOL; the "Touch diagnostics" boot entry (NegroniTouchDiag) shows it.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/EFIClock.h>
#include <Protocol/LoadedImage.h>

#include "tlmm.h"
#include "geni_i2c.h"
#include "syna_tcm.h"
#include "rpmh.h"
#include "EFII2C.h"
#include "NegroniTouchLog.h"

#define SE4_BASE   0x00990000UL
#define S3908_ADDR 0x4b

STATIC struct geni_i2c  mI2c = { (volatile uint8_t *)SE4_BASE, 0, 0 };
STATIC struct tcm       mTcm;
STATIC CHAR8            mLog[NEGRONI_TOUCH_LOG_SIZE];
STATIC UINTN            mLogLen;
STATIC EFI_GUID         mLogProtocolGuid = NEGRONI_TOUCH_LOG_PROTOCOL_GUID;
STATIC EFI_GUID         mDiagFileGuid    = NEGRONI_TOUCH_DIAG_FILE_GUID;

STATIC EFI_STATUS EFIAPI Rerun (VOID);

STATIC NEGRONI_TOUCH_LOG_PROTOCOL  mLogProtocol = { mLog, Rerun };

STATIC CONST CHAR8 *mClocks[] = {
  "gcc_qupv3_wrap_0_m_ahb_clk",
  "gcc_qupv3_wrap_0_s_ahb_clk",
  "gcc_qupv3_wrap0_core_clk",
  "gcc_qupv3_wrap0_core_2x_clk",
  "gcc_qupv3_wrap0_s4_clk",
};

STATIC
VOID
EFIAPI
Log (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  UINTN    Len;

  VA_START (Args, Format);
  Len = AsciiVSPrint (mLog + mLogLen, sizeof (mLog) - mLogLen, Format, Args);
  VA_END (Args);
  DEBUG ((DEBUG_ERROR, "NTOUCH: %a", mLog + mLogLen));
  mLogLen += Len;
}

// SE4 FIFO interface is disabled by the firmware on negroni and Qualcomm's GpiDxe owns the GPI engine:
// transfers go through Qualcomm's I2CDxe (EFI_QCOM_I2C_PROTOCOL), which drives the SE in GPI mode.
STATIC EFI_GUID               mQcomI2cGuid = QCOM_I2C_PROTOCOL_GUID;
STATIC EFI_QCOM_I2C_PROTOCOL  *mQcomI2c;
STATIC VOID                   *mQcomHandle;
STATIC UINT32                 mQcomInstance;
STATIC I2C_SLAVE_CONFIG       mQcomCfg = { I2C_FAST_MODE_FREQ_KHZ, S3908_ADDR, I2C, 2500, 0, 0 };

STATIC int QcomStatus (I2C_STATUS St)
{
  return St == I2C_SUCCESS ? GENI_I2C_OK : (St == I2C_ERROR_ADDR_NACK || St == I2C_ERROR_DATA_NACK) ? GENI_I2C_NACK :
         St == I2C_ERROR_TRANSFER_TIMEOUT ? GENI_I2C_TIMEOUT : GENI_I2C_BUS_ERR;
}

STATIC int QcomXfer (VOID *Handle, uint8_t *Buf, uint32_t Len, UINT32 Flags, I2C_STATUS *Raw)
{
  I2C_DESCRIPTOR  Desc = { Buf, Len, Flags };
  UINT32          Done = 0;
  I2C_STATUS      St   = mQcomI2c->Transfer (Handle, &mQcomCfg, &Desc, 1, NULL, NULL, 0, &Done);

  if (Raw != NULL) {
    *Raw = St;
  }

  return QcomStatus (St);
}

STATIC int I2cRead (void *Ctx, uint8_t *Buf, uint32_t Len)
{
  return mQcomHandle ? QcomXfer (mQcomHandle, Buf, Len, I2C_READ_TRANSFER, NULL) : geni_i2c_read (Ctx, S3908_ADDR, Buf, Len);
}

STATIC int I2cWrite (void *Ctx, const uint8_t *Buf, uint32_t Len)
{
  return mQcomHandle ? QcomXfer (mQcomHandle, (uint8_t *)Buf, Len, I2C_WRITE_TRANSFER, NULL) : geni_i2c_write (Ctx, S3908_ADDR, Buf, Len);
}
STATIC void Wait1ms (void) { gBS->Stall (1000); }
STATIC void WaitUs (unsigned Us) { gBS->Stall (Us); }

STATIC CONST CHAR8 *
RpmhText (
  int  Rc
  )
{
  switch (Rc) {
    case 0: return "ON (OK)";
    case RH_E_GEOMETRY: return "FAILED (RPMh controller not recognised)";
    case RH_E_CMDDB: return "FAILED (cmd-db unreadable)";
    case RH_E_LOOKUP: return "FAILED (regulator not in cmd-db)";
    case RH_E_BUSY: return "FAILED (controller busy)";
    case RH_E_SEND: return "FAILED (write did not stick)";
    case RH_E_TIMEOUT: return "FAILED (timeout)";
    default: return "FAILED";
  }
}

STATIC CONST CHAR8 *
I2cText (
  int  Rc
  )
{
  switch (Rc) {
    case GENI_I2C_OK: return "answers (OK)";
    case GENI_I2C_NACK: return "NO ANSWER (NACK, unpowered or wrong address)";
    case GENI_I2C_BUS_ERR: return "BUS ERROR";
    case GENI_I2C_TIMEOUT: return "TIMEOUT (bus not clocking)";
    default: return "FAILED";
  }
}

STATIC
VOID
DumpTcs (
  VOID
  )
{
  struct rh_tcs_state  S;
  unsigned             T;

  Log ("rpmh irq_en 0x%x irq_status 0x%x\n", rh_rd (RH_TCS_BASE + RH_IRQ_EN), rh_rd (RH_TCS_BASE + RH_IRQ_STATUS));
  for (T = 0; T < RH_ACTIVE_N; T++) {
    rh_tcs_dump (T, &S);
    Log (
      "tcs%u idle %x ctl %x en %x wait %x | %x=%x st %x | %x=%x st %x | %x=%x st %x\n",
      T, S.idle, S.control, S.enable, S.wait,
      S.addr[0], S.data[0], S.status[0], S.addr[1], S.data[1], S.status[1], S.addr[2], S.data[2], S.status[2]
      );
  }
}

// S3908 supplies (downstream DT synaptics20031@4B): vcc_1v8 = L6C 1.8 V, vdd_2v8 = L3C 3.008 V
STATIC
VOID
PowerOn (
  CONST CHAR8  *Res,
  unsigned     Mv
  )
{
  struct rh_diag  D = { 0 };
  int             Rc = rh_vreg_enable (Res, Mv, WaitUs, &D);

  Log ("Power %a %u mV: %a [%d]\n", Res, Mv, RpmhText (Rc), Rc);
  if (Rc != 0) {
    Log ("  id 0x%x config 0x%x tcs %u ncpt %u db v%u addr 0x%x tcs%u\n", D.id, D.config, D.tcs_count, D.ncpt, D.db_version, D.address, D.tcs);
  }
  if (Rc != 0) {
    DumpTcs ();
  }
}

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
    Log ("no clock protocol %r\n", Status);
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mClocks); Index++) {
    Status = Clock->GetClockID (Clock, mClocks[Index], &Id);
    if (!EFI_ERROR (Status)) {
      Status = Clock->EnableClock (Clock, Id);
    }

    if (EFI_ERROR (Status)) {
      Log ("%a %r\n", mClocks[Index], Status);
    }
  }

  // 19.2 MHz SE clock: the 400 kHz divider table assumes it
  if (!EFI_ERROR (Clock->GetClockID (Clock, "gcc_qupv3_wrap0_s4_clk", &Id))) {
    UINT32  Hz = 0;

    Status = Clock->SetClockFreqHz (Clock, Id, 19200000, EFI_CLOCK_FREQUENCY_HZ_CLOSEST, &Hz);
    Log ("SE4 clock %u Hz %r\n", Hz, Status);
  }
}

STATIC
VOID
Probe (
  VOID
  )
{
  volatile UINT8  *Tlmm = (volatile UINT8 *)(UINTN)TLMM_BASE;
  int             Rc;
  UINTN           Try;

  mLogLen = 0;
  mLog[0] = 0;

  EnableClocks ();
  // power: 1.8 V (L6C + load switch), then 2.8 V (L3C); reset held low throughout
  tlmm_out (Tlmm, NT_PIN_RESET, 0);
  PowerOn ("ldoc6", 1800);
  nt_pins_setup (Tlmm);
  PowerOn ("ldoc3", 3008);
  gBS->Stall (200 * 1000);            // synaptics,power-delay-ms
  tlmm_out (Tlmm, NT_PIN_RESET, 1);   // release reset
  gBS->Stall (200 * 1000);            // synaptics,reset-delay-ms: firmware boot
  Log ("pins: reset %d 1v8-en %d attn %d\n",
       (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_RESET)) >> 1) & 1),
       (int)((*(volatile UINT32 *)(Tlmm + TLMM_IO (NT_PIN_1V8_EN)) >> 1) & 1),
       tlmm_in (Tlmm, NT_PIN_IRQ));

  // bus state with the pins as GPIO inputs (pull-ups): both should read 1 on an idle bus
  Log ("I2C lines: %a [sda %d scl %d]\n",
       (tlmm_in (Tlmm, NT_PIN_I2C_SDA) && tlmm_in (Tlmm, NT_PIN_I2C_SCL)) ? "free (OK)" : "STUCK LOW",
       tlmm_in (Tlmm, NT_PIN_I2C_SDA), tlmm_in (Tlmm, NT_PIN_I2C_SCL));
  if (!tlmm_in (Tlmm, NT_PIN_I2C_SDA) || !tlmm_in (Tlmm, NT_PIN_I2C_SCL)) {
    Log ("I2C bus recovery: %a\n", nt_i2c_recover (Tlmm, WaitUs) ? "freed (OK)" : "still stuck");
  }

  nt_pins_setup_i2c (Tlmm);
  Rc = geni_i2c_init (&mI2c);
  Log ("SE4 proto %u fifo %u init %d fw 0x%x clk_cfg 0x%x ios 0x%x (bit0 sda bit1 scl)\n", geni_i2c_proto (&mI2c), mI2c.tx_depth, Rc,
       geni_rd (&mI2c, GENI_FW_REVISION_RO), geni_rd (&mI2c, GENI_SER_M_CLK_CFG), geni_rd (&mI2c, SE_GENI_IOS));
  if (Rc != GENI_I2C_OK) {
    return;
  }

  {
    BOOLEAN  FifoOff = (geni_rd (&mI2c, GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE) != 0;

    Log ("Transfer mode: %a\n", FifoOff ? "GPI (firmware disabled the FIFO) via Qualcomm I2CDxe" : "FIFO");
    if (FifoOff && (mQcomHandle == NULL)) {
      if (EFI_ERROR (gBS->LocateProtocol (&mQcomI2cGuid, NULL, (VOID **)&mQcomI2c))) {
        Log ("Qualcomm I2C protocol: NOT FOUND\n");
        return;
      }

      // find the instance that is QUP0 SE4: the one where the S3908 answers
      for (UINT32 Inst = 1; Inst <= 24 && mQcomHandle == NULL; Inst++) {
        VOID        *H = NULL;
        uint8_t     B[4];
        I2C_STATUS  St;

        DEBUG ((DEBUG_ERROR, "NTOUCH: trying I2C instance %u\n", Inst));   // a hang shows which one
        St = mQcomI2c->Open (Inst, &H);

        if (St != I2C_SUCCESS) {
          continue;
        }

        if (QcomXfer (H, B, sizeof (B), I2C_READ_TRANSFER, &St) == GENI_I2C_OK) {
          mQcomHandle   = H;
          mQcomInstance = Inst;
        } else {
          mQcomI2c->Close (H);
        }

        Log ("  I2C instance %u: %a [%d]\n", Inst, St == I2C_SUCCESS ? "touch chip answers (OK)" :
             (St == I2C_ERROR_ADDR_NACK) ? "no chip at 0x4b" : "error", St);
      }

      Log ("Qualcomm I2C: %a instance %u\n", mQcomHandle ? "using (OK)" : "NO INSTANCE REACHES THE CHIP", mQcomInstance);
      if (mQcomHandle == NULL) {
        return;
      }
    }
  }

  {
    uint8_t  Raw[4] = { 0 };
    int      R      = I2cRead (&mI2c, Raw, 4);

    Log ("Touch chip: %a [%d] data %02x %02x %02x %02x\n", I2cText (R), R, Raw[0], Raw[1], Raw[2], Raw[3]);
    Log ("geni status 0x%x m_irq 0x%x ios 0x%x\n", geni_rd (&mI2c, SE_GENI_STATUS), geni_rd (&mI2c, SE_GENI_M_IRQ_STATUS), geni_rd (&mI2c, SE_GENI_IOS));
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

  Log ("TouchComm message: %a [%d code 0x%x len %u]\n", Rc == 0 ? "received (OK)" : Rc > 0 ? "none waiting" : "FAILED (no 0xA5 marker / bus error)",
       Rc, mTcm.code, mTcm.payload_len);
  if ((Rc == 0) && (mTcm.code == TCM_REPORT_IDENTIFY) && (mTcm.payload_len >= 2)) {
    Log ("identify version %u mode %u\n", mTcm.payload[0], mTcm.payload[1]);
  }

  Rc = tcm_get_report_config (&mTcm, Wait1ms);
  Log ("Touch report config: %a [%d, %u bytes]\n", Rc == 0 ? "OK" : "FAILED", Rc, mTcm.config_len);
  Log ("Interrupt line: %a\n", tlmm_in (Tlmm, NT_PIN_IRQ) ? "idle (high)" : "message waiting (low)");

}

STATIC
EFI_STATUS
EFIAPI
Rerun (
  VOID
  )
{
  Probe ();
  return EFI_SUCCESS;
}

// "Touch diagnostics" boot entry: NegroniTouchDiag in the same FV as this driver, last in BootOrder.
STATIC
VOID
EFIAPI
AddDiagBootOption (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_LOADED_IMAGE_PROTOCOL          *Image;
  EFI_DEVICE_PATH_PROTOCOL           *FvPath;
  EFI_DEVICE_PATH_PROTOCOL           *Path;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_BOOT_MANAGER_LOAD_OPTION       Option;
  EFI_BOOT_MANAGER_LOAD_OPTION       *Options;
  UINTN                              Count;
  EFI_STATUS                         Status;
  VOID                               *VariableWrite;

  if (EFI_ERROR (gBS->LocateProtocol (&gEfiVariableWriteArchProtocolGuid, NULL, &VariableWrite))) {
    return;   // the protocol notify fires again once variables are writable
  }

  gBS->CloseEvent (Event);
  Status = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image);
  if (EFI_ERROR (Status)) {
    return;
  }

  FvPath = DevicePathFromHandle (Image->DeviceHandle);
  if (FvPath == NULL) {
    return;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &mDiagFileGuid);
  Path = AppendDevicePathNode (FvPath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  if (Path == NULL) {
    return;
  }

  Status = EfiBootManagerInitializeLoadOption (&Option, LoadOptionNumberUnassigned, LoadOptionTypeBoot, LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP,
             L"Touch diagnostics", Path, NULL, 0);
  if (!EFI_ERROR (Status)) {
    Options = EfiBootManagerGetLoadOptions (&Count, LoadOptionTypeBoot);
    // an entry from an older build without CATEGORY_APP would sit in BootOrder ahead of Windows
    for (UINTN Index = 0; Index < Count; Index++) {
      if ((StrCmp (Options[Index].Description, L"Touch diagnostics") == 0) &&
          ((Options[Index].Attributes & LOAD_OPTION_CATEGORY) != LOAD_OPTION_CATEGORY_APP))
      {
        EfiBootManagerDeleteLoadOptionVariable (Options[Index].OptionNumber, LoadOptionTypeBoot);
      }
    }
    if (EfiBootManagerFindLoadOption (&Option, Options, Count) < 0) {
      Status = EfiBootManagerAddLoadOptionVariable (&Option, (UINTN)-1);
      DEBUG ((DEBUG_ERROR, "NTOUCH: boot entry \"Touch diagnostics\" %r\n", Status));
    }

    EfiBootManagerFreeLoadOptions (Options, Count);
    EfiBootManagerFreeLoadOption (&Option);
  }

  FreePool (Path);
}

EFI_STATUS
EFIAPI
NegroniTouchDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle = NULL;
  EFI_EVENT   Event;
  VOID        *Registration;

  Probe ();
  gBS->InstallMultipleProtocolInterfaces (&Handle, &mLogProtocolGuid, &mLogProtocol, NULL);

  // Boot#### needs writable variables
  if (!EFI_ERROR (gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, AddDiagBootOption, NULL, &Event))) {
    gBS->RegisterProtocolNotify (&gEfiVariableWriteArchProtocolGuid, Event, &Registration);
    gBS->SignalEvent (Event);
  }

  return EFI_SUCCESS;
}
