## negroni: Project Mu FrontPage FV contents (Common/Mu_OEM_Sample/FrontpageFdf.inc)
# Continuation of [FV.FvMain]
  INF MsGraphicsPkg/GopOverrideDxe/GopOverrideDxe.inf
  INF DfciPkg/AuthManagerNull/AuthManagerNull.inf
  INF DfciPkg/SettingsManager/SettingsManagerDxe.inf
  INF MsGraphicsPkg/MsUiTheme/Dxe/MsUiThemeProtocol.inf
  INF MsGraphicsPkg/RenderingEngineDxe/RenderingEngineDxe.inf
  INF MsGraphicsPkg/DisplayEngineDxe/DisplayEngineDxe.inf
  INF OemPkg/BootMenu/BootMenu.inf
  INF OemPkg/FrontPage/FrontPage.inf
  #negroni (Silicium FV has it): INF PcBdsPkg/MsBootPolicy/MsBootPolicy.inf
  INF MdeModulePkg/Universal/BootManagerPolicyDxe/BootManagerPolicyDxe.inf
  INF MdeModulePkg/Universal/RegularExpressionDxe/RegularExpressionDxe.inf
  INF DfciPkg/DfciManager/DfciManager.inf
  INF MsGraphicsPkg/OnScreenKeyboardDxe/OnScreenKeyboardDxe.inf
  INF OemPkg/FrontpageButtonsVolumeUp/FrontpageButtonsVolumeUp.inf
  INF MsGraphicsPkg/SimpleWindowManagerDxe/SimpleWindowManagerDxe.inf
  # Change AARCH64 to the appropriate architecture for your platform.
  FILE APPLICATION=PCD(gPcBdsPkgTokenSpaceGuid.PcdShellFile) {
    SECTION PE32=$(OUTPUT_DIRECTORY)/$(TARGET)_$(TOOL_CHAIN_TAG)/AARCH64/Shell.efi
    SECTION UI= "EdkShell"
  }
  # negroni (Silicium boot logo kept, same GUID): FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdLogoFile) {
  # negroni (Silicium boot logo kept, same GUID): SECTION RAW = OemPkg/FrontPage/Resources/BootLogo.bmp
  # negroni (Silicium boot logo kept, same GUID): SECTION UI = "Logo"
  # negroni (Silicium boot logo kept, same GUID): }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdFrontPageLogoFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/FrontpageLogo.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdLowBatteryFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/LBAT.bmp
  }
  FILE FREEFORM = PCD(gOemPkgTokenSpaceGuid.PcdThermalFile) {
    SECTION RAW = OemPkg/FrontPage/Resources/THOT.bmp
  }