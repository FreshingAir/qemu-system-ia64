// SPDX-License-Identifier: GPL-2.0-or-later

DefinitionBlock ("", "DSDT", 2, "QEMU  ", "I2K4DSDT", 0x00000001)
{
    Name (_S5, Package (0x04)
    {
        0x04,
        0x04,
        Zero,
        Zero
    })

    Scope (_SB)
    {
        Device (PCI0)
        {
            Name (_HID, "PNP0A03")
            Name (_CID, "PNP0A03")
            Name (_SEG, Zero)
            Name (_BBN, Zero)
            Name (_UID, Zero)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 0, 0x001F, 0, 0x0020)
                QWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0, 0x00003FFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, Cacheable, ReadWrite,
                    0, 0x000A0000, 0x000BFFFF, 0, 0x00020000)
                DWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, Cacheable, ReadOnly,
                    0, 0x000C0000, 0x000C7FFF, 0, 0x00008000)
                QWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0x90000000, 0x9FFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package ()
            {
                Package () { 0x0005FFFF, 0, Zero, 16 },
                Package () { 0x0003FFFF, 0, Zero, 16 },
                Package () { 0x0002FFFF, 3, Zero, 19 }
            })

            Device (IFB0)
            {
                Name (_ADR, 0x00020000)

                Device (COM1)
                {
                    Name (_HID, EisaId ("PNP0501"))
                    Name (_UID, Zero)
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x03F8, 0x03F8, 1, 8)
                        IRQ (Edge, ActiveHigh, Exclusive) {4}
                    })
                }

                Device (PS2K)
                {
                    Name (_HID, EisaId ("PNP0303"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IO (Decode16, 0x0060, 0x0060, 1, 1)
                        IO (Decode16, 0x0064, 0x0064, 1, 1)
                        IRQNoFlags () {1}
                    })
                }

                Device (PS2M)
                {
                    Name (_HID, EisaId ("PNP0F13"))
                    Name (_CRS, ResourceTemplate ()
                    {
                        IRQNoFlags () {12}
                    })
                }
            }
        }

        Device (PCI1)
        {
            Name (_HID, "PNP0A03")
            Name (_CID, "PNP0A03")
            Name (_SEG, Zero)
            Name (_BBN, 0x20)
            Name (_UID, One)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 0x0020, 0x003F, 0, 0x0020)
                QWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x00004000, 0x00007FFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                QWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0xA0000000, 0xAFFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package ()
            {
                Package () { 0x0002FFFF, 0, Zero, 20 },
                Package () { 0x0003FFFF, 0, Zero, 20 }
            })
        }

        Device (PCI2)
        {
            Name (_HID, "PNP0A03")
            Name (_CID, "PNP0A03")
            Name (_SEG, Zero)
            Name (_BBN, 0x40)
            Name (_UID, 0x02)
            Name (_CCA, One)
            Name (_CRS, ResourceTemplate ()
            {
                WordBusNumber (ResourceProducer, MinFixed, MaxFixed,
                    PosDecode, 0, 0x0040, 0x005F, 0, 0x0020)
                QWordIO (ResourceProducer, MinFixed, MaxFixed, PosDecode,
                    EntireRange, 0, 0x00008000, 0x0000BFFF, 0, 0x00004000,
                    , , , TypeStatic, DenseTranslation)
                QWordMemory (ResourceProducer, PosDecode, MinFixed,
                    MaxFixed, NonCacheable, ReadWrite,
                    0, 0xB0000000, 0xBFFFFFFF, 0, 0x10000000)
            })
            Name (_PRT, Package () {})
        }

    }
}
