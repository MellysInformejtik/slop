#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/UsbIo.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/UsbFunctionIo.h>

STATIC VOID Inventory(EFI_GUID *Guid, CHAR16 *Name, BOOLEAN Devices)
{
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, Index;
  EFI_STATUS Status = gBS->LocateHandleBuffer(ByProtocol, Guid, NULL, &Count, &Handles);
  Print(L"%s: %r, handles=%u\r\n", Name, Status, Count);
  if (!EFI_ERROR(Status) && Devices) {
    for (Index = 0; Index < Count; Index++) {
      EFI_USB_IO_PROTOCOL *Usb;
      EFI_USB_DEVICE_DESCRIPTOR Descriptor;
      Status = gBS->HandleProtocol(Handles[Index], Guid, (VOID **)&Usb);
      if (!EFI_ERROR(Status)) {
        Status = Usb->UsbGetDeviceDescriptor(Usb, &Descriptor);
        if (!EFI_ERROR(Status)) {
          Print(L"  %04x:%04x class=%02x\r\n", Descriptor.IdVendor, Descriptor.IdProduct, Descriptor.DeviceClass);
        } else {
          Print(L"  descriptor: %r\r\n", Status);
        }
      }
    }
  }
  if (Handles != NULL) FreePool(Handles);
}

EFI_STATUS EFIAPI NegroniUsbDiagEntry(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_INPUT_KEY Key;
  UINTN Index;
  (VOID)ImageHandle;
  (VOID)SystemTable;
  do {
    gST->ConOut->ClearScreen(gST->ConOut);
    Print(L"Negroni USB protocol inventory\r\n\r\n");
    Inventory(&gEfiUsb2HcProtocolGuid, L"Host controller", FALSE);
    Inventory(&gEfiUsbFunctionIoProtocolGuid, L"PC/device controller", FALSE);
    Inventory(&gEfiUsbIoProtocolGuid, L"Attached USB interfaces", TRUE);
    Print(L"\r\nA controller handle does not prove transfers work.\r\n");
    Print(L"R: refresh after plugging in a peripheral. Other key: exit.\r\n");
    gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
    if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) continue;
    if (Key.UnicodeChar != L'r' && Key.UnicodeChar != L'R') break;
  } while (TRUE);
  return EFI_SUCCESS;
}
