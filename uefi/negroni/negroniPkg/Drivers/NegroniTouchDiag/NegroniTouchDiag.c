/** @file
  "Touch diagnostics" boot entry: shows the NegroniTouchDxe probe log on screen.
  Volume Up reruns the power-on and probe; any other key returns to the firmware menu.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "../NegroniTouchDxe/NegroniTouchLog.h"

STATIC EFI_GUID  mLogProtocolGuid = NEGRONI_TOUCH_LOG_PROTOCOL_GUID;

STATIC
VOID
Show (
  IN CONST NEGRONI_TOUCH_LOG_PROTOCOL  *Touch
  )
{
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"negroni touch diagnostics (S3908 @0x4b, QUP0 SE4)\n\n");
  Print (L"%a\n", Touch->Log);
  Print (L"Volume Up: power on and probe again   other key: back\n");
}

EFI_STATUS
EFIAPI
NegroniTouchDiagEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  NEGRONI_TOUCH_LOG_PROTOCOL  *Touch;
  EFI_INPUT_KEY               Key;
  UINTN                       Index;
  EFI_STATUS                  Status;

  Status = gBS->LocateProtocol (&mLogProtocolGuid, NULL, (VOID **)&Touch);
  if (EFI_ERROR (Status)) {
    Print (L"NegroniTouchDxe is not loaded (%r)\n", Status);
    gBS->Stall (5 * 1000 * 1000);
    return Status;
  }

  for ( ; ;) {
    Show (Touch);
    gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if (Key.ScanCode != SCAN_UP) {
      return EFI_SUCCESS;
    }

    Print (L"\nprobing...\n");
    Touch->Rerun ();
  }
}
