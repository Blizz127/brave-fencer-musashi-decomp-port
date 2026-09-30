"""Second geometry ranges and their fetch seam.

Words come from the local disc extract (skipped when absent); retail code is
no longer exported in src/ (tests/test_no_retail_words.py).
"""

from pathlib import Path
import re
import struct

from retail_local import asm_words, check_published_ranges, exe_words
from retail_local import overlay as local_overlay


ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "80014b10.c": (0x80014B10, 0x80014BFC, 59),
    "8002d8d4.c": (0x8002D8D4, 0x8002D904, 12),
    "800476c0.c": (0x800476C0, 0x8004787C, 111),
    "8004768c.c": (0x8004768C, 0x800476C0, 13),
    "80046610.c": (0x80046610, 0x80046630, 8),
    "800469cc.c": (0x800469CC, 0x80046A5C, 36),
    "80046994.c": (0x80046994, 0x800469A8, 5),
    "800469a8.c": (0x800469A8, 0x800469CC, 9),
    "80046cfc.c": (0x80046CFC, 0x80046D1C, 8),
    "80046bfc.c": (0x80046BFC, 0x80046C38, 15),
    "800468fc.c": (0x800468FC, 0x80046980, 33),
    "80046abc.c": (0x80046ABC, 0x80046B44, 34),
    "8004689c.c": (0x8004689C, 0x800468FC, 24),
    "80043088.c": (0x80043088, 0x800430B8, 12),
    "80046d1c.c": (0x80046D1C, 0x8004768C, 604),
    "80046c38.c": (0x80046C38, 0x80046CFC, 49),
    "80046b44.c": (0x80046B44, 0x80046BFC, 46),
    "8004657c.c": (0x8004657C, 0x80046610, 37),
    "8001bfb0.c": (0x8001BFB0, 0x8001BFD0, 8),
    "8001bfa0.c": (0x8001BFA0, 0x8001BFB0, 4),
    "8003d650.c": (0x8003D650, 0x8003D6E4, 37),
    "8002d858.c": (0x8002D858, 0x8002D8A8, 20),
    "80018730.c": (0x80018730, 0x80018918, 122),
    "80018714.c": (0x80018714, 0x80018730, 7),
    "8002f5c8.c": (0x8002F5C8, 0x8002F620, 22),
    "8002f658.c": (0x8002F658, 0x8002F67C, 9),
    "80029504.c": (0x80029504, 0x80029514, 4),
    "8002f620.c": (0x8002F620, 0x8002F648, 10),
    "8002f648.c": (0x8002F648, 0x8002F658, 4),
    "8001af04.c": (0x8001AF04, 0x8001AF34, 12),
    "800191bc.c": (0x800191BC, 0x800191D4, 6),
    "800191d4.c": (0x800191D4, 0x8001923C, 26),
    "80011d54.c": (0x80011D54, 0x80011DA0, 19),
    "80011cfc.c": (0x80011CFC, 0x80011D54, 22),
    "8001b85c.c": (0x8001B85C, 0x8001B86C, 4),
    "8001c0c8.c": (0x8001C0C8, 0x8001C14C, 33),
    "8001b384.c": (0x8001B384, 0x8001B394, 4),
    "80011dcc.c": (0x80011DCC, 0x80011DF4, 10),
    "80029254.c": (0x80029254, 0x80029264, 4),
    "8002aef8.c": (0x8002AEF8, 0x8002AF08, 4),
    "80014444.c": (0x80014444, 0x800144D4, 36),
    "8004923c.c": (0x8004923C, 0x80049248, 3),
    "80014554.c": (0x80014554, 0x800145EC, 38),
    "8002850c.c": (0x8002850C, 0x80028558, 19),
    "80053af8.c": (0x80053AF8, 0x80053B20, 10),
    "800491fc.c": (0x800491FC, 0x8004921C, 8),
    "8002823c.c": (0x8002823C, 0x80028304, 50),
    "80028620.c": (0x80028620, 0x80028694, 29),
    "80053328.c": (0x80053328, 0x80053804, 311),
    "8005386c.c": (0x8005386C, 0x800538BC, 20),
    "80053804.c": (0x80053804, 0x8005386C, 26),
    "800538bc.c": (0x800538BC, 0x800538EC, 12),
    "80052d90.c": (0x80052D90, 0x80052E38, 42),
    "800538ec.c": (0x800538EC, 0x80053978, 35),
    "8001c044.c": (0x8001C044, 0x8001C0C8, 33),
    "8005c358.c": (0x8005C358, 0x8005C388, 12),
    "80015310.c": (0x80015310, 0x8001534C, 15),
    "80028a48.c": (0x80028A48, 0x80028C50, 130),
    "80028c50.c": (0x80028C50, 0x80028D58, 66),
    "80014390.c": (0x80014390, 0x800143AC, 7),
    "800191a8.c": (0x800191A8, 0x800191BC, 5),
    "80010a98.c": (0x80010A98, 0x80010AE0, 18),
    "800595fc.c": (0x800595FC, 0x80059658, 23),
    "80042610.c": (0x80042610, 0x80042644, 13),
    "800189a8.c": (0x800189A8, 0x80018A20, 30),
    "80018a20.c": (0x80018A20, 0x80018C64, 145),
    "8005d184.c": (0x8005D184, 0x8005D244, 48),
    "8005f704.c": (0x8005F704, 0x8005F728, 9),
    "8005d244.c": (0x8005D244, 0x8005D4B8, 157),
    "80028d58.c": (0x80028D58, 0x80028D78, 8),
    "80029178.c": (0x80029178, 0x800291A0, 10),
    "8005d538.c": (0x8005D538, 0x8005D6A0, 90),
    "8005e188.c": (0x8005E188, 0x8005E1A4, 7),
    "8005d4b8.c": (0x8005D4B8, 0x8005D538, 32),
    "8005e79c.c": (0x8005E79C, 0x8005E8E8, 83),
    "80018c64.c": (0x80018C64, 0x80018CE8, 33),
    "80018f88.c": (0x80018F88, 0x80018FC8, 16),
    "80018ce8.c": (0x80018CE8, 0x80018E78, 100),
    "80018f20.c": (0x80018F20, 0x80018F88, 26),
    "80018e9c.c": (0x80018E9C, 0x80018F20, 33),
    "800291b4.c": (0x800291B4, 0x800291C8, 5),
    "80028d78.c": (0x80028D78, 0x80028D9C, 9),
    "80015208.c": (0x80015208, 0x800152F4, 59),
    "80059bfc.c": (0x80059BFC, 0x80059CF4, 62),
    "8005ae80.c": (0x8005AE80, 0x8005AF68, 58),
    "8005c054.c": (0x8005C054, 0x8005C1C0, 91),
    "80010b40.c": (0x80010B40, 0x80010BB4, 29),
    "80010bb4.c": (0x80010BB4, 0x80010C7C, 50),
    "80029690.c": (0x80029690, 0x800296F8, 26),
    "80029274.c": (0x80029274, 0x8002931C, 42),
    "8002941c.c": (0x8002941C, 0x80029444, 10),
    "80029344.c": (0x80029344, 0x8002941C, 54),
    "80010ae0.c": (0x80010AE0, 0x80010B10, 12),
    "80011b7c.c": (0x80011B7C, 0x80011C10, 37),
    "80011778.c": (0x80011778, 0x80011818, 40),
    "80015498.c": (0x80015498, 0x80015608, 92),
    "80058b40.c": (0x80058B40, 0x80058B7C, 15),
    "8001c00c.c": (0x8001C00C, 0x8001C044, 14),
    "8001f9f8.c": (0x8001F9F8, 0x8001FB8C, 101),
    "8001d1c4.c": (0x8001D1C4, 0x8001D388, 113),
    "800184f0.c": (0x800184F0, 0x80018714, 137),
    "800596f4.c": (0x800596F4, 0x80059760, 27),
    "8005bed8.c": (0x8005BED8, 0x8005C020, 82),
    "8002cdd8.c": (0x8002CDD8, 0x8002CFE4, 131),
    "8003d25c.c": (0x8003D25C, 0x8003D32C, 52),
    "8002cfe4.c": (0x8002CFE4, 0x8002D034, 20),
    "8002d904.c": (0x8002D904, 0x8002DC68, 217),
    "80037fc4.c": (0x80037FC4, 0x8003819C, 118),
    "8002ec10.c": (0x8002EC10, 0x8002ED90, 96),
    "8003836c.c": (0x8003836C, 0x800383A4, 14),
    "8003c4f0.c": (0x8003C4F0, 0x8003C598, 42),
    "80030f80.c": (0x80030F80, 0x800314DC, 343),
    "8002d320.c": (0x8002D320, 0x8002D4B8, 102),
    "8002ee64.c": (0x8002EE64, 0x8002EE90, 11),
    "8002eff8.c": (0x8002EFF8, 0x8002F064, 27),
    "800314dc.c": (0x800314DC, 0x800316F8, 135),
    "800316f8.c": (0x800316F8, 0x80031988, 164),
    "80031988.c": (0x80031988, 0x80031A98, 68),
    "8003819c.c": (0x8003819C, 0x800381E4, 18),
    "80038210.c": (0x80038210, 0x80038308, 62),
    "80038308.c": (0x80038308, 0x8003834C, 17),
    "8003834c.c": (0x8003834C, 0x8003836C, 8),
    "80038638.c": (0x80038638, 0x80038668, 12),
    "8003b250.c": (0x8003B250, 0x8003B280, 12),
    "8003c23c.c": (0x8003C23C, 0x8003C438, 127),
    "8003d3b4.c": (0x8003D3B4, 0x8003D3F8, 17),
    "80031be0.c": (0x80031BE0, 0x80031CC8, 58),
    "8002f064.c": (0x8002F064, 0x8002F0D0, 27),
    "80030ca4.c": (0x80030CA4, 0x80030D80, 55),
    "80030d80.c": (0x80030D80, 0x80030F80, 128),
    "800336a8.c": (0x800336A8, 0x8003388C, 121),
    "8003388c.c": (0x8003388C, 0x800342E8, 663),
    "80034650.c": (0x80034650, 0x800346D0, 32),
    "8003f144.c": (0x8003F144, 0x8003F380, 143),
    "8003f380.c": (0x8003F380, 0x8003F434, 45),
    "800414e4.c": (0x800414E4, 0x800415A8, 49),
    "8005ba90.c": (0x8005BA90, 0x8005BD7C, 187),
    "8001099c.c": (0x8001099C, 0x80010A08, 27),
    "80058ce4.c": (0x80058CE4, 0x80058D20, 15),
    "80059cf4.c": (0x80059CF4, 0x80059D68, 29),
    "8003500c.c": (0x8003500C, 0x800351E8, 119),
    "8002d034.c": (0x8002D034, 0x8002D1F0, 111),
    "8002d240.c": (0x8002D240, 0x8002D29C, 23),
    "8002d29c.c": (0x8002D29C, 0x8002D320, 33),
    "8002dc68.c": (0x8002DC68, 0x8002DF80, 198),
    "8002df80.c": (0x8002DF80, 0x8002E138, 110),
    "8002e138.c": (0x8002E138, 0x8002E5BC, 289),
    "8002e5bc.c": (0x8002E5BC, 0x8002E5F8, 15),
    "8002e5f8.c": (0x8002E5F8, 0x8002E638, 16),
    "8002e638.c": (0x8002E638, 0x8002E700, 50),
    "8002e700.c": (0x8002E700, 0x8002E79C, 39),
    "8002e79c.c": (0x8002E79C, 0x8002E818, 31),
    "8002e818.c": (0x8002E818, 0x8002E8DC, 49),
    "8002e8dc.c": (0x8002E8DC, 0x8002E94C, 28),
    "8002e94c.c": (0x8002E94C, 0x8002EA10, 49),
    "8002ea10.c": (0x8002EA10, 0x8002EAB0, 40),
    "8002eab0.c": (0x8002EAB0, 0x8002EB10, 24),
    "8002eb10.c": (0x8002EB10, 0x8002EBAC, 39),
    "8002ebac.c": (0x8002EBAC, 0x8002EC10, 25),
    "8002ede4.c": (0x8002EDE4, 0x8002EE64, 32),
    "8002ee90.c": (0x8002EE90, 0x8002EEB8, 10),
    "8002eeb8.c": (0x8002EEB8, 0x8002EED8, 8),
    "8002eed8.c": (0x8002EED8, 0x8002EFD0, 62),
    "8002efd0.c": (0x8002EFD0, 0x8002EFF8, 10),
    "8002f0d0.c": (0x8002F0D0, 0x8002F0F4, 9),
    "8002f248.c": (0x8002F248, 0x8002F4E4, 167),
    "8002f4e4.c": (0x8002F4E4, 0x8002F5C8, 57),
    "8002f714.c": (0x8002F714, 0x8002F80C, 62),
    "80031b7c.c": (0x80031B7C, 0x80031BE0, 25),
    "80031cc8.c": (0x80031CC8, 0x80031D70, 42),
    "80031dec.c": (0x80031DEC, 0x80031E94, 42),
    "80031e94.c": (0x80031E94, 0x80031F14, 32),
    "80031f14.c": (0x80031F14, 0x80031FC8, 45),
    "80031fc8.c": (0x80031FC8, 0x80032048, 32),
    "80032048.c": (0x80032048, 0x800322A8, 152),
    "800322a8.c": (0x800322A8, 0x800324A4, 127),
    "800324a4.c": (0x800324A4, 0x80032774, 180),
    "80032774.c": (0x80032774, 0x80032A74, 192),
    "80032a74.c": (0x80032A74, 0x8003310C, 422),
    "8003310c.c": (0x8003310C, 0x800331D4, 50),
    "800331d4.c": (0x800331D4, 0x8003324C, 30),
    "8003324c.c": (0x8003324C, 0x80033324, 54),
    "80033398.c": (0x80033398, 0x8003350C, 93),
    "8003350c.c": (0x8003350C, 0x800335B8, 43),
    "800335b8.c": (0x800335B8, 0x800336A8, 60),
    "80034314.c": (0x80034314, 0x80034650, 207),
    "800346d0.c": (0x800346D0, 0x800347C8, 62),
    "80034844.c": (0x80034844, 0x800348A8, 25),
    "800348a8.c": (0x800348A8, 0x8003491C, 29),
    "8003491c.c": (0x8003491C, 0x80034A54, 78),
    "80034a54.c": (0x80034A54, 0x80034A9C, 18),
    "80034a9c.c": (0x80034A9C, 0x80034AE0, 17),
    "80034ae0.c": (0x80034AE0, 0x80034B0C, 11),
    "80034b0c.c": (0x80034B0C, 0x80034B3C, 12),
    "80034cf0.c": (0x80034CF0, 0x80034DFC, 67),
    "80034dfc.c": (0x80034DFC, 0x8003500C, 132),
    "80036d58.c": (0x80036D58, 0x80036EB4, 87),
    "80036eb4.c": (0x80036EB4, 0x80036EE8, 13),
    "80036ee8.c": (0x80036EE8, 0x80036F18, 12),
    "80036f18.c": (0x80036F18, 0x80036F98, 32),
    "800381e4.c": (0x800381E4, 0x80038210, 11),
    "800383a4.c": (0x800383A4, 0x800384A8, 65),
    "800384a8.c": (0x800384A8, 0x800385C0, 70),
    "800385c0.c": (0x800385C0, 0x80038638, 30),
    "80038668.c": (0x80038668, 0x80038698, 12),
    "80038698.c": (0x80038698, 0x800387C0, 74),
    "800387c0.c": (0x800387C0, 0x80038838, 30),
    "80038838.c": (0x80038838, 0x800388E8, 44),
    "80038908.c": (0x80038908, 0x80038958, 20),
    "8003916c.c": (0x8003916C, 0x800391D4, 26),
    "8003ab84.c": (0x8003AB84, 0x8003AE14, 164),
    "8003af4c.c": (0x8003AF4C, 0x8003AFF0, 41),
    "8003b08c.c": (0x8003B08C, 0x8003B0B8, 11),
    "8003b0b8.c": (0x8003B0B8, 0x8003B0E4, 11),
    "8003c0a0.c": (0x8003C0A0, 0x8003C23C, 103),
    "8003c92c.c": (0x8003C92C, 0x8003CF68, 399),
    "8003cff8.c": (0x8003CFF8, 0x8003D134, 79),
    "8003ede8.c": (0x8003EDE8, 0x8003EEE4, 63),
    "80043974.c": (0x80043974, 0x80043994, 8),
    "80044a8c.c": (0x80044A8C, 0x80044B14, 34),
    "8005ce98.c": (0x8005CE98, 0x8005CEA4, 3),
    "8001a0fc.c": (0x8001A0FC, 0x8001A114, 6),
    "8001a114.c": (0x8001A114, 0x8001A338, 137),
    "8001af34.c": (0x8001AF34, 0x8001B0D4, 104),
    "8001b0d4.c": (0x8001B0D4, 0x8001B22C, 86),
    "8001b394.c": (0x8001B394, 0x8001B3C4, 12),
    "8001b3c4.c": (0x8001B3C4, 0x8001B710, 211),
    "8001b710.c": (0x8001B710, 0x8001B788, 30),
    "8001b788.c": (0x8001B788, 0x8001B7C4, 15),
    "8001b7c4.c": (0x8001B7C4, 0x8001B85C, 38),
    "8002d4c8.c": (0x8002D4C8, 0x8002D59C, 53),
    "8002d7fc.c": (0x8002D7FC, 0x8002D80C, 4),
    "8002ed90.c": (0x8002ED90, 0x8002EDE4, 21),
    "8002fd14.c": (0x8002FD14, 0x8002FDC8, 45),
    "8002fde8.c": (0x8002FDE8, 0x8002FF0C, 73),
    "8002ff0c.c": (0x8002FF0C, 0x800301A4, 166),
    "80030730.c": (0x80030730, 0x80030A14, 185),
    "80030a14.c": (0x80030A14, 0x80030CA4, 164),
    "80031a98.c": (0x80031A98, 0x80031B7C, 57),
    "800363cc.c": (0x800363CC, 0x80036AF8, 459),
    "80036af8.c": (0x80036AF8, 0x80036D24, 139),
    "80037144.c": (0x80037144, 0x80037334, 124),
    "80037334.c": (0x80037334, 0x80037358, 9),
    "80037358.c": (0x80037358, 0x80037368, 4),
    "80037368.c": (0x80037368, 0x800373D0, 26),
    "800373d0.c": (0x800373D0, 0x800374CC, 63),
    "800374cc.c": (0x800374CC, 0x8003750C, 16),
    "8003750c.c": (0x8003750C, 0x8003775C, 148),
    "8003775c.c": (0x8003775C, 0x800377D8, 31),
    "80037cd8.c": (0x80037CD8, 0x80037D74, 39),
    "80037d74.c": (0x80037D74, 0x80037D98, 9),
    "800401cc.c": (0x800401CC, 0x800401E4, 6),
    "800415a8.c": (0x800415A8, 0x8004162C, 33),
    "8004162c.c": (0x8004162C, 0x800416A8, 31),
    "800416a8.c": (0x800416A8, 0x800419B0, 194),
    "800419b0.c": (0x800419B0, 0x80041A20, 28),
    "80041a80.c": (0x80041A80, 0x80041AB0, 12),
    "80041ab0.c": (0x80041AB0, 0x80041E8C, 247),
    "80041e8c.c": (0x80041E8C, 0x80042154, 178),
    "80042374.c": (0x80042374, 0x8004239C, 10),
    "800439d4.c": (0x800439D4, 0x800439F8, 9),
    "80046630.c": (0x80046630, 0x8004674C, 71),
    "8004674c.c": (0x8004674C, 0x8004689C, 84),
    "8001aaa0.c": (0x8001AAA0, 0x8001AAD0, 12),
    "80014cac.c": (0x80014CAC, 0x80014CF8, 19),
    "8001aa98.c": (0x8001AA98, 0x8001AAA0, 2),
    "8001534c.c": (0x8001534C, 0x800153A4, 22),
    "800153a4.c": (0x800153A4, 0x800153CC, 10),
    "80010da0.c": (0x80010DA0, 0x80010DE0, 16),
    "800110cc.c": (0x800110CC, 0x80011144, 30),
    "80011220.c": (0x80011220, 0x8001125C, 15),
    "8001125c.c": (0x8001125C, 0x800112A8, 19),
    "800111bc.c": (0x800111BC, 0x80011220, 25),
    "80011144.c": (0x80011144, 0x800111BC, 30),
    "8001a9f8.c": (0x8001A9F8, 0x8001AA78, 32),
    "80010f80.c": (0x80010F80, 0x800110CC, 83),
    "80029514.c": (0x80029514, 0x80029524, 4),
    "80010ed4.c": (0x80010ED4, 0x80010F80, 43),
    "80014338.c": (0x80014338, 0x80014390, 22),
    "80010d60.c": (0x80010D60, 0x80010DA0, 16),
    "8002fdc8.c": (0x8002FDC8, 0x8002FDE8, 8),
    "8002fc64.c": (0x8002FC64, 0x8002FD14, 44),
    "8003c438.c": (0x8003C438, 0x8003C498, 24),
    "8003c498.c": (0x8003C498, 0x8003C4F0, 22),
    "8003ae14.c": (0x8003AE14, 0x8003AE9C, 34),
    "800301a4.c": (0x800301A4, 0x800301C8, 9),
    "8002fb08.c": (0x8002FB08, 0x8002FC64, 87),
    "8001abbc.c": (0x8001ABBC, 0x8001ACF0, 77),
    "8001acf0.c": (0x8001ACF0, 0x8001AE90, 104),
    "8001ae90.c": (0x8001AE90, 0x8001AF04, 29),
    "800149e0.c": (0x800149E0, 0x80014B10, 76),
    "80014c28.c": (0x80014C28, 0x80014C54, 11),
    "80014d30.c": (0x80014D30, 0x80014D68, 14),
    "80014cf8.c": (0x80014CF8, 0x80014D30, 14),
    "800167f0.c": (0x800167F0, 0x800168B4, 49),
    "800118ac.c": (0x800118AC, 0x80011928, 31),
    "80010a84.c": (0x80010A84, 0x80010A98, 5),
    "800146b0.c": (0x800146B0, 0x80014774, 49),
    "800527c4.c": (0x800527C4, 0x8005283C, 30),
    "80053eec.c": (0x80053EEC, 0x80053F6C, 32),
    "8001bff8.c": (0x8001BFF8, 0x8001C00C, 5),
    "80011928.c": (0x80011928, 0x80011998, 28),
    "8001bfe8.c": (0x8001BFE8, 0x8001BFF8, 4),
    "800116e0.c": (0x800116E0, 0x80011778, 38),
    "80029000.c": (0x80029000, 0x80029020, 8),
    "80028d9c.c": (0x80028D9C, 0x80028DBC, 8),
    "80028fbc.c": (0x80028FBC, 0x80028FDC, 8),
    "800183e0.c": (0x800183E0, 0x80018450, 28),
    "800167b8.c": (0x800167B8, 0x800167F0, 14),
    "80016224.c": (0x80016224, 0x80016450, 139),
    "80010a08.c": (0x80010A08, 0x80010A84, 31),
    "80016638.c": (0x80016638, 0x800166E8, 44),
    "80010cec.c": (0x80010CEC, 0x80010D60, 29),
    "80011818.c": (0x80011818, 0x800118AC, 37),
    "80011a3c.c": (0x80011A3C, 0x80011ADC, 40),
    "80011c10.c": (0x80011C10, 0x80011C8C, 31),
    "8005b1c4.c": (0x8005B1C4, 0x8005B400, 143),
    "80010c7c.c": (0x80010C7C, 0x80010CEC, 28),
    "8001513c.c": (0x8001513C, 0x80015144, 2),
    "80015608.c": (0x80015608, 0x80015760, 86),
    "80015760.c": (0x80015760, 0x80015908, 106),
    "800599b8.c": (0x800599B8, 0x80059A80, 50),
    "80053d44.c": (0x80053D44, 0x80053E28, 57),
    "80058890.c": (0x80058890, 0x80058A4C, 111),
    "8001fb8c.c": (0x8001FB8C, 0x8001FC08, 31),
    "8001d388.c": (0x8001D388, 0x8001D3FC, 29),
    "80015908.c": (0x80015908, 0x80015954, 19),
    "80015a74.c": (0x80015A74, 0x80015AD0, 23),
    "8005b75c.c": (0x8005B75C, 0x8005B7B0, 21),
    "80058b7c.c": (0x80058B7C, 0x80058C6C, 60),
    "8005e8e8.c": (0x8005E8E8, 0x8005EA34, 83),
    "8004974c.c": (0x8004974C, 0x8004978C, 16),
    "80052fcc.c": (0x80052FCC, 0x80053050, 33),
    "80054514.c": (0x80054514, 0x800547D8, 177),
    "80048eac.c": (0x80048EAC, 0x80048FB8, 67),
    "80053050.c": (0x80053050, 0x80053178, 74),
    "8005d8a0.c": (0x8005D8A0, 0x8005D8B4, 5),
    "8005d980.c": (0x8005D980, 0x8005D9C4, 17),
    "8005d9c4.c": (0x8005D9C4, 0x8005DBD8, 133),
    "8005dbd8.c": (0x8005DBD8, 0x8005DCA0, 50),
    "8005eae8.c": (0x8005EAE8, 0x8005EB28, 16),
    "8005dca0.c": (0x8005DCA0, 0x8005DE78, 118),
    "8005e0ac.c": (0x8005E0AC, 0x8005E13C, 36),
    "8005fba8.c": (0x8005FBA8, 0x8005FBC8, 8),
    "8005f75c.c": (0x8005F75C, 0x8005F830, 53),
    "8005ed4c.c": (0x8005ED4C, 0x8005F0C8, 223),
    "8005f290.c": (0x8005F290, 0x8005F384, 61),
    "8005fa94.c": (0x8005FA94, 0x8005FB70, 55),
    "8005f394.c": (0x8005F394, 0x8005F450, 47),
    "8005f450.c": (0x8005F450, 0x8005F6CC, 159),
    "8005f6cc.c": (0x8005F6CC, 0x8005F704, 14),
    "8005de78.c": (0x8005DE78, 0x8005E0AC, 141),
    "8005eb28.c": (0x8005EB28, 0x8005EC00, 54),
    "8005ec00.c": (0x8005EC00, 0x8005ECC0, 48),
    "8005ecc0.c": (0x8005ECC0, 0x8005ED4C, 35),
    "8005f384.c": (0x8005F384, 0x8005F394, 4),
    "8005e374.c": (0x8005E374, 0x8005E3AC, 14),
    "8005e3ac.c": (0x8005E3AC, 0x8005E79C, 252),
    "8005ea88.c": (0x8005EA88, 0x8005EAA8, 8),
    "8005eac8.c": (0x8005EAC8, 0x8005EAE8, 8),
}


def _range_in_fmt(fmt, lo, hi):
    """True when the formatter dispatches [lo, hi).

    The 2026-09-15 refactor (d9f78a3a8) moved most of formatter_fetch's
    per-PC `else if (pc >= lo && pc < hi)` branches into sorted
    kFormatterRanges* tables. A range is therefore present either as the
    original literal guard or as a table row `{ 0xlo, 0xhi, ... }`.
    """
    if f"cpu->pc >= 0x{lo:x}u && cpu->pc < 0x{hi:x}u" in fmt:
        return True
    return re.search(rf"\{{\s*0x{lo:x}u\s*,\s*0x{hi:x}u\s*,", fmt) is not None


def _dispatch_pos(fmt, symbol):
    """Source position of the fetch branch that serves the word array `symbol`.

    A symbol is either assigned directly (`instruction = <symbol>`) or a row in
    a kFormatterRanges* table consulted by `formatter_range_word(<table>, ...)`.
    Comparing these positions is how the ordering tests below express overlay
    precedence, which the range-table refactor preserved.
    """
    literal = fmt.find(f"instruction = {symbol}")
    if literal != -1:
        return literal
    row = re.search(rf"\{{\s*0x[0-9a-f]+u\s*,\s*0x[0-9a-f]+u\s*,\s*{re.escape(symbol)}\s*\}}", fmt)
    if row is None:
        return -1
    table = None
    for m in re.finditer(r"static const FormatterRange (\w+)\[\]", fmt):
        if m.start() < row.start():
            table = m.group(1)
        else:
            break
    if table is None:
        return -1
    return fmt.find(f"formatter_range_word({table},")


def test_second_geometry_ranges_match_asm_and_manifest():
    check_published_ranges("second-geom-source-exports.json", EXPECTED, 22483)


def test_8005cea8_testevent_veneer_matches_pinned_exe():
    """SOURCE_IRQ TestEvent veneer [8005CEA8,8005CEB4) is B0:0B."""
    assert exe_words(0x8005CEA8, 0x8005CEB4) == [0x240A00B0, 0x01400008, 0x2409000B]
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert "kFile8005CEA8Words" in fmt
    assert _range_in_fmt(fmt, 0x8005cea8, 0x8005ceb4)


def test_8003ac08_sh_matches_pinned_exe_and_asm():
    """Live CD IRQ STOP at 8003AC08 is sh $v0, 0x1A6($v1) inside 8003AB84."""
    word, = exe_words(0x8003AC08, 0x8003AC0C)
    assert word == 0xA46201A6 == asm_words()[0x8003AC08]


def test_overlay_0010_and_0012_ranges_are_dispatched():
    """Title (member0010) and member0012 streams are on the fetch seam."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert "kOverlay0010_800CEDFCWords" in fmt
    assert _range_in_fmt(fmt, 0x800cedfc, 0x800cf104)
    assert "kOverlay0010_800CF94CWords" in fmt
    assert _range_in_fmt(fmt, 0x800cf94c, 0x800cf990)
    assert "kOverlay0012_80128228Words" in fmt
    assert "kOverlay0012_80128A28Words" in fmt
    for lo, hi, name in ((0x8017BEBC, 0x8017BEE4, "8017BEBC"),
                         (0x8017BEE4, 0x8017BF38, "8017BEE4"),
                         (0x8017BF38, 0x8017BF70, "8017BF38"),
                         (0x8017C008, 0x8017C080, "8017C008")):
        assert f"kOverlay0012_{name}Words" in fmt
        assert _range_in_fmt(fmt, lo, hi)


def test_overlay_800cedfc_member0000_is_dispatched_by_shape():
    overlay = local_overlay("main/0000")
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert "kOverlay800CEDFCWords" in fmt
    assert _range_in_fmt(fmt, 0x800cedfc, 0x800cee40)
    words2 = struct.unpack_from("<49I", overlay, 0x848)
    assert "kOverlay800CEE40Words" in fmt
    assert _range_in_fmt(fmt, 0x800cee40, 0x800cef04)
    # Run off the disc: the formatter keeps the stream's shape, not its words.
    assert re.search(rf"kOverlay800CEE40Words\[{len(words2)}\];", fmt)
    assert f"0x{words2[0]:08x}u" not in fmt.lower()
    for start in (0x800CEF54, 0x800CEFA8, 0x800CEFF4, 0x800CF038,
                  0x800CF0C4, 0x800CF114, 0x800CF180):
        assert f"kOverlay{start:08X}Words" in fmt


def test_overlay_800cee2c_member0007_wins_its_pcs():
    """Member0007 is served from its own bytes, not the opening overlay."""
    overlay = local_overlay("main/0007")
    offset = 0x800CEE2C - 0x800CEDF8
    words = struct.unpack_from("<128I", overlay, offset)
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert "kOverlay800CEE2CWords" in fmt
    assert _range_in_fmt(fmt, 0x800cee2c, 0x800cf02c)
    member0 = _dispatch_pos(fmt, "kOverlay800CEE40Words")
    member7 = _dispatch_pos(fmt, "kOverlay800CEE2CWords")
    cedfc = _dispatch_pos(fmt, "kOverlay800CEDFCWords")
    member10 = _dispatch_pos(fmt, "kOverlay0010_800CEDFCWords")
    assert member7 != -1 and member0 != -1 and member7 < member0
    # kOverlay800CEDFCWords includes 800CEE2C as member0000's epilogue
    # (lw ra,0x10(sp) → 80078E78). Overlay 0007 must win that PC first.
    # Member0010 title overlay is selected instead of 0007 after 8001125C.
    assert cedfc != -1 and member7 < cedfc
    assert member10 != -1 and member10 < member7
    assert "cpu->pc = 0x800cee2cu" not in fmt
    table = struct.unpack_from("<4I", overlay, 0x800CFADC - 0x800CEDF8)
    assert table == (0x800CEEFC, 0x800CEF60, 0x800CEF94, 0x800CEFF8)
    # table[0] 800CEEFC is inside the exported span
    idx = (0x800CEEFC - 0x800CEE2C) // 4
    assert words[idx] == 0x27BDFFE8


def test_overlay_800cf02c_member0007_remainder_is_dispatched():
    """Rest of member0007 after 800CEE2C: 800CF02C dispatcher, OPDEMO, Start."""
    overlay = local_overlay("main/0007")
    offset = 0x800CF02C - 0x800CEDF8
    words = struct.unpack_from("<2259I", overlay, offset)
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert "kOverlay800CF02CWords" in fmt
    assert _range_in_fmt(fmt, 0x800cf02c, 0x800d1378)
    remainder = _dispatch_pos(fmt, "kOverlay800CF02CWords")
    member0 = _dispatch_pos(fmt, "kOverlay800CEE40Words")
    cf038 = _dispatch_pos(fmt, "kOverlay800CF038Words")
    assert remainder != -1 and member0 != -1 and remainder < member0
    assert cf038 != -1 and remainder < cf038
    assert "cpu->pc = 0x800cf3b0u" not in fmt
    table = struct.unpack_from("<8I", overlay, 0x800D133C - 0x800CEDF8)
    assert table[0] == 0x800CF068
    assert table[3] == 0x800CF148
    idx = (0x800CF068 - 0x800CF02C) // 4
    assert words[idx] == 0x27BDFFE8
    # The native runner must retain the EXE call target.
    assert "cpu->pc == 0x8001aa98u" in fmt
    assert "cpu->pc = 0x8001aaa0u" not in fmt


def test_8005d9c4_tap_complete_matches_pinned_exe():
    """8005D9C4 TAP/29B4 serial continuation is word-identical to EXE/asm."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert _range_in_fmt(fmt, 0x8005d8a0, 0x8005d8b4)
    assert _range_in_fmt(fmt, 0x8005d980, 0x8005d9c4)
    assert _range_in_fmt(fmt, 0x8005d9c4, 0x8005dbd8)
    assert "kInputSerialStartWords" in fmt


def test_8005dbd8_eae8_tap_complete_path_is_fetched():
    """8005DBD8 dispatch and 8005EAE8 digital TAP are on the native fetch seam."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert _range_in_fmt(fmt, 0x8005dbd8, 0x8005dca0)
    assert _range_in_fmt(fmt, 0x8005eae8, 0x8005eb28)
    assert _range_in_fmt(fmt, 0x8005dca0, 0x8005de78)
    assert _range_in_fmt(fmt, 0x8005e0ac, 0x8005e13c)
    assert _range_in_fmt(fmt, 0x8005fba8, 0x8005fbc8)
    assert _range_in_fmt(fmt, 0x8005f75c, 0x8005f830)


def test_8005ed4c_f290_29b4_path_is_fetched():
    """8005ED4C TAP stage and 8005F290 29B4 zeroing are on the native fetch seam."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert _range_in_fmt(fmt, 0x8005ed4c, 0x8005f0c8)
    assert _range_in_fmt(fmt, 0x8005f290, 0x8005f384)
    assert _range_in_fmt(fmt, 0x8005fa94, 0x8005fb70)
    assert _range_in_fmt(fmt, 0x8005f394, 0x8005f450)
    assert _range_in_fmt(fmt, 0x8005f450, 0x8005f6cc)
    assert _range_in_fmt(fmt, 0x8005f6cc, 0x8005f704)
    assert _range_in_fmt(fmt, 0x8005de78, 0x8005e0ac)
    assert _range_in_fmt(fmt, 0x8005eb28, 0x8005ec00)
    assert _range_in_fmt(fmt, 0x8005ec00, 0x8005ecc0)
    assert _range_in_fmt(fmt, 0x8005ecc0, 0x8005ed4c)
    assert _range_in_fmt(fmt, 0x8005f384, 0x8005f394)


def test_8005e374_e3ac_unmapped_tap_callees_are_fetched():
    """8005E228 jals 8005E374/8005E3AC; those TAP callees are now fetched 1:1."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    assert _range_in_fmt(fmt, 0x8005e374, 0x8005e3ac)
    assert _range_in_fmt(fmt, 0x8005e3ac, 0x8005e79c)
    assert _range_in_fmt(fmt, 0x8005ea88, 0x8005eaa8)
    assert _range_in_fmt(fmt, 0x8005eac8, 0x8005eae8)
    assert "0x8005e31cu" in fmt and "0x8005e374u" in fmt
    assert "0x8005e34cu" in fmt and "0x8005e3acu" in fmt


def test_getpadbuf_runs_the_retail_getter():
    """8005F704 is lui 0x8008 / addiu 0x8A48: addiu sign-extends, so the retail
    getter returns 80078A48 (+0xF0 when a0&0xF0), the InitPAD PadInfo itself.
    The formatter once replaced its return with [80072990]+(a0&0xF0), read
    from a misdecoded 80088A48; that diverged for port 2 and is gone."""
    fmt = (ROOT / "pc_port/mips_formatter.c").read_text()
    words = exe_words(0x8005F704, 0x8005F70C)
    assert words[0] == 0x3C028008
    assert words[1] == 0x24428A48
    assert (0x80080000 + ((words[1] & 0xFFFF) ^ 0x8000) - 0x8000) == 0x80078A48
    assert "cpu->pc == 0x8005f704u" not in fmt
    assert "cpu->r[2] = base + (cpu->r[4] & 0xf0u)" not in fmt


def test_overlay_0010_800cfbbc_live_stop_is_dispatched():
    overlay = local_overlay('main/0010')
    offset = 0x20000 + 0x800CFBBC - 0x800CEDF8
    assert struct.unpack_from('<4I', overlay, offset) == (0x3C018011, 0xA4204E70, 0x03E00008, 0)
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    assert _range_in_fmt(fmt, 0x800cfbbc, 0x800cfbcc)
    assert 'kOverlay0010_800CFBBCWords' in fmt


def test_overlay_0010_800cfbcc_800cfbe8_live_stop_is_dispatched():
    # Refused at 800CFBE8 on the room transition out of the New Game opening
    # cutscene; the resident guest RAM there equals member0010.
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    for start, end, name in ((0x800CFBCC, 0x800CFBE8, '800CFBCC'),
                             (0x800CFBE8, 0x800CFBF8, '800CFBE8')):
        assert _range_in_fmt(fmt, start, end)
        assert f'kOverlay0010_{name}Words' in fmt


def test_overlay_0010_800d0488_live_stop_is_dispatched():
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    assert _range_in_fmt(fmt, 0x800d0488, 0x800d0588)
    assert 'kOverlay0010_800D0488Words' in fmt


def test_overlay_0010_800cf370_live_stop_is_dispatched():
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    assert _range_in_fmt(fmt, 0x800cf370, 0x800cf398)
    assert 'kOverlay0010_800CF370Words' in fmt


def test_overlay_0010_800cf398_live_stop_is_dispatched():
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    assert _range_in_fmt(fmt, 0x800cf398, 0x800cf3b8)
    assert 'kOverlay0010_800CF398Words' in fmt


def test_title_overlay_unmapped_words_do_not_fall_back_to_member_zero():
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    guard = fmt.index('else if (g_overlay_0004_words || g_overlay_0007_words || g_overlay_0010_words)\n        return 0;')
    fallback = _dispatch_pos(fmt, 'kOverlay800CF290Words')
    title = _dispatch_pos(fmt, 'kOverlay0010_800CF398Words')
    assert title < guard < fallback


def test_overlay_0010_800cf3b8_live_stop_is_dispatched():
    fmt = (ROOT / 'pc_port/mips_formatter.c').read_text()
    assert _range_in_fmt(fmt, 0x800cf3b8, 0x800cf47c)
    assert 'kOverlay0010_800CF3B8Words' in fmt


