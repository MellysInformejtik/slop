// Negroni SM8450 dual-role USB enumeration.
// Resource evidence: sm8450-oneplus-negroni.dtb, usb@a600000.
// SPI 133 + 32 = GSI 165 (0xA5). No GPIO/VBUS writes or forced host role.
// Requires a compatible Qualcomm role/connector/PHY driver stack in Windows.
// This table alone cannot initialize clocks, PHY, SMMU or source VBUS.
DefinitionBlock ("UsbSsdt.aml", "SSDT", 2, "NEGRON", "NGUSB   ", 1)
{
    Scope (\_SB)
    {
        Device (URS0)
        {
            Name (_HID, "QCOM0497") // Same SM8450 controller ID as ingres.
            Name (_CID, "PNP0CA1")
            Name (_SUB, "MTP08450")
            Name (_UID, Zero)
            Name (_CCA, Zero)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x0A600000, 0x000FC100)
            })
            Method (_STA, 0, NotSerialized) { Return (0x0F) }

            // URS enumerates exactly one child stack for the negotiated role.
            Device (USB0)
            {
                Name (_ADR, Zero)
                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 0xA5 }
                })
            }
            Device (UFN0)
            {
                Name (_ADR, One)
                Name (_CRS, ResourceTemplate ()
                {
                    Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 0xA5 }
                })
            }
        }
    }
}
