// negroni touchscreen (Synaptics S3908 on QUP0 SE4 I2C) for the Windows NegroniTouch driver.
// The driver maps SE registers and TLMM GPIO21 (ATTN), GPIO16/17 (SDA/SCL); Mu's NegroniTouchDxe
// leaves clocks, pins and power on at ExitBootServices.
DefinitionBlock ("TouchSsdt.aml", "SSDT", 2, "SWEET ", "NEGTOUCH", 1)
{
    Scope (\_SB)
    {
        Device (NTCH)
        {
            Name (_HID, "NEGT0001")
            Name (_UID, 0)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x00990000, 0x00004000)   // QUP0 SE4 (i2c4)
                Memory32Fixed (ReadWrite, 0x0F115000, 0x00001000)   // TLMM GPIO21 (touch ATTN)
                Memory32Fixed (ReadWrite, 0x0F110000, 0x00002000)   // GPIO16/17 open-drain I2C fallback
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
    }
}
