##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = negroni
  PLATFORM_GUID                  = 1F8B6A9C-41F2-4C2C-8A91-B8C03F8ED7E6
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/negroniPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = negroniPkg/negroni.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

!include WaipioPkg/WaipioPkg.dsc.inc

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"OnePlus 10 Pro 5G"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"negroni"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"OnePlus_10_Pro_5G_negroni"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"OnePlus 10 Pro 5G"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2412
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  # (no UniXNU: plain Mu BDS) UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  #
  # Memory Libraries
  #
  MemoryMapLib|negroniPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|negroniPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  # (no UniXNU: plain Mu BDS) UniXNUPkg/Application/UniXNU/UniXNU.inf
  # (no UniXNU: plain Mu BDS) UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  # (no UniXNU: plain Mu BDS) DarwinBootPkg/Driver/DarwinBoot.inf
  negroniPkg/Drivers/NegroniBootMemoryDxe/NegroniBootMemoryDxe.inf
  negroniPkg/Drivers/NegroniKeyInputDxe/NegroniKeyInputDxe.inf

  #
  # ACPI Tables
  #
  negroni/AcpiTables.inf
