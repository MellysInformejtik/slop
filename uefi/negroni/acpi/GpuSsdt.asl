// SM8450 / Adreno 730 resources for the experimental Windows ARM64 WDDM port.
// Addresses are cross-checked against the shipped SM8450 DTB and Negroni XNU
// bring-up patches. This table only describes resources; it does not touch HW.
// Interrupt resources are intentionally omitted until the GIC DT-to-ACPI GSI
// mapping has been verified on this firmware.
DefinitionBlock ("GpuSsdt.aml", "SSDT", 2, "SWEET ", "NEGRGPU", 1)
{
    Scope (\_SB)
    {
        Device (GPU0)
        {
            Name (_HID, "QCOM0A73")
            Name (_UID, 0)
            Name (_CRS, ResourceTemplate ()
            {
                // The A730 clock/power sequence uses GCC_GPU_* registers at
                // 0x00100000 + offsets through 0x81154. XNU maps this same
                // 0x1f4200 aperture. The Negroni UEFI register map has no
                // other descriptor in this range; keep it owned by GPU0.
                Memory32Fixed (ReadWrite, 0x00100000, 0x001F4200) // GCC GPU clocks
                Memory32Fixed (ReadWrite, 0x03D00000, 0x00040000) // Adreno 730 core
                Memory32Fixed (ReadWrite, 0x03D6A000, 0x00035000) // GMU
                Memory32Fixed (ReadWrite, 0x03D50000, 0x00010000) // GMU RSCC
                Memory32Fixed (ReadWrite, 0x03DA0000, 0x00040000) // Adreno SMMU
                Memory32Fixed (ReadWrite, 0x0B290000, 0x00010000) // GMU PDC
                Memory32Fixed (ReadWrite, 0x03D61000, 0x00000800) // GMU debug
                // GPU-CC (0x03D90000/0xA000) and CX-misc
                // (0x03D9E000/0x1000) are contained within the GMU window
                // above. Do not publish overlapping PnP resources; the KMD
                // derives those register views as offsets into that mapping.
            })
            Method (_STA, 0, NotSerialized)
            {
                Return (0x0F)
            }
        }
    }
}
