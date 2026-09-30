"""Hand-maintained status of each Psy-Q entry point (read by tools/psyq_callsites.py).

status  "psycross"  PsyCross implements it: pc_port/platform/psyq/psyq_psycross.c
        "hle"       our own C: pc_port/platform/psyq/bfm_psyq_compat.c
        "stub-TBD"  no host version yet: the native lane refuses the PC and the
                    interpreter keeps running the retail code from the user's disc
                    against the device HLE (so a stub is slow, not broken).

INFERRED names addresses the vendor symbol files leave unnamed; each carries its
evidence. Nothing here is Psy-Q code: only names, argument counts and notes.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Status:
    status: str
    argc: int = 0
    returns: int = 0
    wrapper: str | None = None
    note: str = ""


# address -> (name, evidence)
INFERRED: dict[int, tuple[str, str]] = {
    0x8005C324: ("memcpy", "symbols.resident.txt: memcpy = 0x8005C324 (libc2 MEMCPY.o)"),
    0x8005C2C8: ("bzero", "only function of Psy-Q object BZERO.o"),
    0x8005C358: ("memset", "Psy-Q object MEMSET.o"),
    0x8005C4CC: ("srand", "sw a0, D_80078980; jr ra (first function of the object at 8005C4CC)"),
    # strcmp is func_8005C4DC (NULL rules in bfm_psyq_strcmp); only the
    # libraries call it, so it has no table entry.
    0x8005C388: ("putchar", "Psy-Q object PUTCHAR.o"),
    0x8005C604: ("printf", "Psy-Q object PRNT.o"),
    0x80047CB4: ("InitGeom", "sets H=0x3E8, ZSF3/ZSF4, DQA/DQB, OFX=OFY=0 (asm/main.s func_80047CB4)"),
    0x800599B8: ("LoadImage", "called (RECT *, data) by func_800184F0 per texture record (vendor src/800.c:5761ff)"),
    0x80059CF4: ("DrawOTag", "main loop passes OT base + 0x3FFC after CatPrim (vendor src/boot.c)"),
    0x80059D68: ("PutDrawEnv", "main loop passes &state[buf * 92 + 0x38]: a 0x5C-byte DRAWENV (vendor src/boot.c)"),
    0x80059FC0: ("PutDispEnv", "main loop passes &state[buf * 20 + 0x14C]: a 0x14-byte DISPENV (vendor src/boot.c)"),
    0x80059BFC: ("ClearOTagR", "main loop: (OT base, 0x1000) and (OT, 4) each frame (vendor src/boot.c)"),
    0x800596F4: ("DrawSync", "main loop: func_800596F4(0) right before VSync (vendor src/boot.c)"),
    0x80059658: ("SetDispMask", "called with 1 after the first frame is set up (resident md_MAIN_003)"),
    # libgte matrix routines named from how libgs uses them (repo decomp C)
    0x8004978C: ("RotMatrix", "three angles through the packed sin/cos table D_8006DF1C into m; GsSortSprite calls it with (0, 0, rotate/360)"),
    0x8004901C: ("ScaleMatrix", "column scaling m[i][j] * v[j] >> 12; GsSortSprite calls it with (scalex, scaley, 0)"),
    0x800484EC: ("ApplyMatrixLV", "func_80053050/80052FCC call it as (m, m2->t, &v) for the translation part of a coordinate product"),
    0x80048D9C: ("MulMatrix", "func_80053050: rotation product into the first matrix (m1 = m1 * m2)"),
    0x80048EAC: ("MulMatrix2", "func_80052FCC: rotation product into the second matrix (m2 = m1 * m2); GsSetLightMatrix uses it too"),
    # libgs (from the repo's decompiled C of each address and the splat object names)
    0x80052460: ("GsSortFastSprite", "libgs object 2D_SP1; builds E1 + SPRT (0x64) from a GsSPRITE without scale/rotate and links it into a GsOT"),
    0x80052D90: ("GsInitCoordinate2", "copies GsIDMATRIX into coord, sets super, clears flag, links super->sub (repo src/main/80052d90.c)"),
    0x80052E38: ("GsSetLsMatrix", "SetRotMatrix(m) + SetTransMatrix(m) (repo src/main/80052e38.c)"),
    0x80053308: ("GsSetProjection", "libgs object GS_107; a SetGeomScreen(h) wrapper (repo src/main/80053308.c)"),
    0x80053AF8: ("GsSetAmbient", "SetBackColor(r>>4, g>>4, b>>4) (repo src/main/80053af8.c)"),
    0x800538EC: ("GsSetLightMode", "libgs object GS_108 (with GsSetFogParam); stores mode 0..3 (repo src/main/800538ec.c)"),
    0x80054514: ("GsGetLw", "coordinate-hierarchy walk to a world matrix with the flag cache (repo src/main/80054514.c)"),
    0x800547D8: ("GsGetLs", "GsGetLw-shaped walk, then multiplied with the world-screen matrix (repo src/main/800547d8.c)"),
    0x80054AAC: ("GsGetLws", "GsGetLw/GsGetLs pair in one walk, three arguments (repo src/main/80054aac.c)"),
}

H = "hle"
P = "psycross"
S = "stub-TBD"

GPU_PTR = "packet/struct lives in guest RAM"
TABLE: dict[str, Status] = {
    # --- libgpu: packets and environments in guest RAM, rendered via bfm_plat_renderer
    "GetTPage": Status(H, 4, 1, "bfm_psyq_GetTPage", "pure: (tp<<7)|(abr<<5)|(y>>8<<4)|(x>>6)"),
    "GetClut": Status(H, 2, 1, "bfm_psyq_GetClut", "pure: (y<<6)|(x>>4)"),
    "AddPrim": Status(H, 2, 0, "bfm_psyq_AddPrim", GPU_PTR + "; 24-bit guest links"),
    "CatPrim": Status(H, 2, 0, "bfm_psyq_CatPrim", GPU_PTR),
    "SetSemiTrans": Status(H, 2, 0, "bfm_psyq_SetSemiTrans", GPU_PTR),
    "SetPolyF3": Status(H, 1, 0, "bfm_psyq_SetPolyF3", GPU_PTR + "; header len/code"),
    "SetPolyFT3": Status(H, 1, 0, "bfm_psyq_SetPolyFT3", GPU_PTR),
    "SetPolyG3": Status(H, 1, 0, "bfm_psyq_SetPolyG3", GPU_PTR),
    "SetPolyGT3": Status(H, 1, 0, "bfm_psyq_SetPolyGT3", GPU_PTR),
    "SetPolyF4": Status(H, 1, 0, "bfm_psyq_SetPolyF4", GPU_PTR),
    "SetPolyFT4": Status(H, 1, 0, "bfm_psyq_SetPolyFT4", GPU_PTR),
    "SetPolyG4": Status(H, 1, 0, "bfm_psyq_SetPolyG4", GPU_PTR),
    "SetPolyGT4": Status(H, 1, 0, "bfm_psyq_SetPolyGT4", GPU_PTR),
    "SetLineF2": Status(H, 1, 0, "bfm_psyq_SetLineF2", GPU_PTR),
    "SetLineG2": Status(H, 1, 0, "bfm_psyq_SetLineG2", GPU_PTR),
    "SetLineG3": Status(H, 1, 0, "bfm_psyq_SetLineG3", GPU_PTR + "; polyline terminator 0x55555555"),
    "SetLineG4": Status(H, 1, 0, "bfm_psyq_SetLineG4", GPU_PTR + "; polyline terminator 0x55555555"),
    "SetDefDispEnv": Status(H, 5, 1, "bfm_psyq_SetDefDispEnv", "fills a guest DISPENV; returns the guest pointer; 5th arg from guest stack"),
    "ClearOTagR": Status(H, 2, 1, "bfm_psyq_ClearOTagR", "reverse OT in guest RAM, terminator 0x00FFFFFF"),
    "LoadImage": Status(H, 2, 1, "bfm_psyq_LoadImage", "guest RECT + pixels -> bfm_plat_renderer_upload_vram (texture replacement applies)"),
    "StoreImage": Status(H, 2, 1, "bfm_psyq_StoreImage", "bfm_plat_renderer_download_vram into guest RAM"),
    "MoveImage": Status(H, 3, 1, "bfm_psyq_MoveImage", "download + upload through the renderer"),
    "DrawSync": Status(H, 1, 1, "bfm_psyq_DrawSync", "host rendering is synchronous: returns 0"),
    "SetDispMask": Status(H, 1, 0, "bfm_psyq_SetDispMask", "no-op on the host (display always on)"),
    "PutDrawEnv": Status(H, 1, 1, "bfm_psyq_PutDrawEnv", "guest DRAWENV -> BfmPlatDrawEnv; returns the guest pointer"),
    "PutDispEnv": Status(H, 1, 1, "bfm_psyq_PutDispEnv", "guest DISPENV -> BfmPlatDispEnv; returns the guest pointer"),
    "DrawOTag": Status(H, 1, 0, "bfm_psyq_DrawOTag", "walks the guest OT (24-bit links) and decodes GP0 packets to BfmPlatPrim; unknown codes skipped and counted"),
    "SetDrawEnv": Status(S, 0, 0, None, "builds a DR_ENV packet in guest RAM (TBD)"),
    "DrawSyncCallback": Status(S, 0, 0, None, "callback into guest code: needs the lane's guest-call thunk"),
    # --- libgs: packet builders over guest RAM (pc_port/platform/psyq/bfm_psyq_libgs.c)
    "GsSortLine": Status(H, 3, 0, "bfm_psyq_GsSortLine", "E1 + LINE_F2 into GsOUT_PACKET_P, linked into the guest GsOT; drawn by DrawOTag"),
    "GsSortFastSprite": Status(H, 3, 0, "bfm_psyq_GsSortFastSprite", "E1 + SPRT into GsOUT_PACKET_P, linked into the guest GsOT; drawn by DrawOTag"),
    "GsInitCoordinate2": Status(H, 2, 0, "bfm_psyq_GsInitCoordinate2", "guest GsCOORDINATE2 fields"),
    "GsMapModelingData": Status(H, 1, 0, "bfm_psyq_GsMapModelingData", "relocates a TMD object table in guest RAM once"),
    "GsSetLightMode": Status(H, 1, 0, "bfm_psyq_GsSetLightMode", "libgs global at 0x800C6DC8"),
    "GsSetLsMatrix": Status(P, 1, 0, "bfm_psyq_GsSetLsMatrix", "GTE bridge: SetRotMatrix + SetTransMatrix"),
    "GsSetProjection": Status(P, 1, 0, "bfm_psyq_GsSetProjection", "GTE bridge: SetGeomScreen"),
    "GsSetAmbient": Status(P, 3, 0, "bfm_psyq_GsSetAmbient", "GTE bridge: RBK/GBK/BBK = (c >> 4) << 4"),
    "SetBackColor": Status(P, 3, 0, "bfm_psyq_SetBackColor", "GTE bridge: RBK/GBK/BBK = c << 4"),
    "GsSortSprite": Status(P, 3, 0, "bfm_psyq_GsSortSprite", "GTE bridge: SPRT, or RotMatrix (guest table) + ScaleMatrix + TransMatrix + RotTransPers4 -> POLY_FT4; linked into the guest GsOT"),
    "GsSortBg": Status(S, 0, 0, None, "GsBG cell-map walker with scaling and rotation: large"),
    "GsSortFastBg": Status(S, 0, 0, None, "GsBG cell-map walker (unscaled): moderate; next"),
    "GsGetLw": Status(P, 2, 0, "bfm_psyq_GsGetLw", "GTE bridge: the retail walk (chain in D_800C6D48, stamp D_800C7C70, dirty = flag 0) with ApplyMatrixLV + MulMatrix"),
    "GsGetLs": Status(P, 2, 0, "bfm_psyq_GsGetLs", "GTE bridge: GsGetLw then GsWSMATRIX (x) Lw"),
    "GsGetLws": Status(P, 3, 0, "bfm_psyq_GsGetLws", "GTE bridge: Lw and GsWSMATRIX (x) Lw from one walk"),
    "GsSetRefView2L": Status(S, 0, 0, None, "view matrix from GsRVIEW2 (ratan2, rsin/rcos, matrix products): large"),
    "GsLinkObject5": Status(S, 0, 0, None, "TMD object packet preset (GsDOBJ5): large; 2 call sites"),
    "GsPresetObject": Status(S, 0, 0, None, "TMD object packet preset: large; 2 call sites"),
    "GsInitGraph2": Status(S, 0, 0, None, "display/draw init over libgs globals and libgpu: low call count"),
    "GsSetFogParam": Status(S, 0, 0, None, "fog: 1 call site"),
    # --- libetc
    "VSync": Status(H, 1, 1, "bfm_psyq_VSync", "n>1 waits n fields via bfm_plat_timing_vsync; n=0 one field; n=1 returns 0 (no hcount); n<0 returns the field counter"),
    "ResetCallback": Status(S, 0, 0, None, "interrupt callbacks belong to the IRQ HLE"),
    "DMACallback": Status(S, 0, 0, None, "interrupt callbacks belong to the IRQ HLE"),
    "StopCallback": Status(S, 0, 0, None, "interrupt callbacks belong to the IRQ HLE"),
    # --- libcd
    "CdIntToPos": Status(H, 2, 1, "bfm_psyq_CdIntToPos", "pure: LBA -> BCD CdlLOC in guest RAM"),
    "CdPosToInt": Status(H, 1, 1, "bfm_psyq_CdPosToInt", "pure: BCD CdlLOC -> LBA"),
    "CdSearchFile": Status(H, 2, 1, "bfm_psyq_CdSearchFile", "bfm_plat_disc_find_file -> guest CdlFILE {pos, size, name}; returns the pointer or 0"),
    # --- libgte: only stateless math (PsyCross's GTE has its own register file)
    "ratan2": Status(H, 2, 1, "bfm_psyq_ratan2", "exact port of func_8004CFEC over the guest atan table D_80071F1C"),
    "csqrt": Status(S, 0, 0, None, "stateless but not in PsyCross; fixed-point semantics to pin before an HLE"),
    "catan": Status(S, 0, 0, None, "stateless but not in PsyCross"),
    "SquareRoot12": Status(S, 0, 0, None, "stateless but not in PsyCross"),
    # GTE state through the bridge: PsyCross works on the gteRegs bank gte_owner leases
    "InitGeom": Status(P, 0, 0, "bfm_psyq_InitGeom", "GTE bridge (gte_owner lease)"),
    "SetGeomOffset": Status(P, 2, 0, "bfm_psyq_SetGeomOffset", "GTE bridge (gte_owner lease); the hook site also records the projection"),
    "SetGeomScreen": Status(P, 1, 0, "bfm_psyq_SetGeomScreen", "GTE bridge (gte_owner lease); the hook site also records the projection"),
    "ReadGeomOffset": Status(P, 2, 0, "bfm_psyq_ReadGeomOffset", "GTE bridge (gte_owner lease); reads C2_OFX/OFY"),
    "SetRotMatrix": Status(P, 1, 0, "bfm_psyq_SetRotMatrix", "GTE bridge (gte_owner lease)"),
    "SetTransMatrix": Status(P, 1, 0, "bfm_psyq_SetTransMatrix", "GTE bridge (gte_owner lease)"),
    "SetLightMatrix": Status(P, 1, 0, "bfm_psyq_SetLightMatrix", "GTE bridge (gte_owner lease)"),
    "ReadRotMatrix": Status(P, 1, 1, "bfm_psyq_ReadRotMatrix", "GTE bridge (gte_owner lease)"),
    "MulMatrix0": Status(P, 3, 1, "bfm_psyq_MulMatrix0", "GTE bridge (gte_owner lease); clobbers the rotation matrix like Psy-Q"),
    "MulRotMatrix": Status(P, 1, 1, "bfm_psyq_MulRotMatrix", "GTE bridge (gte_owner lease)"),
    "CompMatrix": Status(P, 3, 1, "bfm_psyq_CompMatrix", "GTE bridge (gte_owner lease)"),
    "CompMatrixLV": Status(P, 3, 1, "bfm_psyq_CompMatrixLV", "GTE bridge (gte_owner lease)"),
    "ApplyRotMatrix": Status(P, 2, 1, "bfm_psyq_ApplyRotMatrix", "GTE bridge (gte_owner lease)"),
    "ApplyRotMatrixLV": Status(P, 2, 1, "bfm_psyq_ApplyRotMatrixLV", "GTE bridge (gte_owner lease)"),
    "ApplyMatrixSV": Status(P, 3, 1, "bfm_psyq_ApplyMatrixSV", "GTE bridge (gte_owner lease); sets the rotation matrix like Psy-Q"),
    "RotTransSV": Status(P, 3, 0, "bfm_psyq_RotTransSV", "GTE bridge (gte_owner lease); long *flag via a host temporary"),
    "RotTransPers": Status(P, 4, 1, "bfm_psyq_RotTransPers", "GTE bridge (gte_owner lease); long *p/*flag via host temporaries"),
    "RotTransPers3": Status(P, 8, 1, "bfm_psyq_RotTransPers3", "GTE bridge (gte_owner lease); args 5-8 from the guest stack; long * via temporaries"),
    "RotTransPers4": Status(P, 10, 1, "bfm_psyq_RotTransPers4", "GTE bridge (gte_owner lease); args 5-10 from the guest stack; long * via temporaries"),
    "RotMatrixYXZ": Status(H, 2, 1, "bfm_psyq_RotMatrixYXZ", "exact port of func_80049A1C over the guest table D_8006DF1C (PsyCross's differs by 1-2)"),
    "RotMatrixX": Status(H, 2, 1, "bfm_psyq_RotMatrixX", "exact port of func_80049F3C over the guest table D_8006DF1C"),
    "RotMatrixY": Status(H, 2, 1, "bfm_psyq_RotMatrixY", "exact port of func_8004A0DC over the guest table D_8006DF1C"),
    "RotMatrixZ": Status(H, 2, 1, "bfm_psyq_RotMatrixZ", "exact port of func_8004A27C over the guest table D_8006DF1C"),
    "PushMatrix": Status(P, 0, 0, "bfm_psyq_PushMatrix", "GTE bridge; the retail guest-RAM stack (offset D_8006DC18, 20 slots at D_8006DC1C), not PsyCross's host stack; refuses when full (the retail error path saves $ra and printf()s)"),
    "PopMatrix": Status(P, 0, 0, "bfm_psyq_PopMatrix", "GTE bridge; the retail guest-RAM stack; refuses when empty (retail error path)"),
    "ApplyMatrixLV": Status(P, 3, 1, "bfm_psyq_ApplyMatrixLV", "GTE bridge; PsyCross's Psy-Q hi/lo split"),
    "MulMatrix": Status(P, 2, 1, "bfm_psyq_MulMatrix", "GTE bridge; sets the rotation matrix like Psy-Q"),
    "MulMatrix2": Status(P, 2, 1, "bfm_psyq_MulMatrix2", "GTE bridge; sets the rotation matrix like Psy-Q"),
    "ReadGeomScreen": Status(P, 0, 1, "bfm_psyq_ReadGeomScreen", "GTE bridge: cfc2 $26"),
    "RotMatrix": Status(H, 2, 1, "bfm_psyq_RotMatrix", "exact retail arithmetic over the game's own sin/cos table in guest RAM"),
    "ScaleMatrix": Status(H, 2, 1, "bfm_psyq_ScaleMatrix", "exact retail arithmetic (m[2][2] writes its whole word)"),
    # --- libc2
    "rand": Status(H, 0, 1, "bfm_psyq_rand", "Psy-Q LCG: next = next*1103515245 + 12345; (next>>16) & 0x7FFF; state is host-side (see notes)"),
    "strcpy": Status(H, 2, 1, "bfm_psyq_strcpy", "guest pointers; NULL dst/src returns 0 without copying; forward byte copy"),
    "srand": Status(H, 1, 0, "bfm_psyq_srand", "stores the seed in D_80078980"),
    "memcpy": Status(H, 3, 1, "bfm_psyq_memcpy", "guest pointers"),
    "memset": Status(H, 3, 1, "bfm_psyq_memset", "guest pointers"),
    "bzero": Status(H, 2, 0, "bfm_psyq_bzero", "guest pointers"),
    "strcmp": Status(H, 2, 1, "bfm_psyq_strcmp", "guest pointers; NULL rules: both/equal 0, a0 NULL -1, a1 NULL 1"),
    "putchar": Status(H, 1, 1, "bfm_psyq_putchar", "to the platform log (line-buffered)"),
    "printf": Status(S, 0, 0, None, "guest varargs + format in guest RAM (TBD)"),
}

SEGMENT_NOTES = {
    "snd": "sound driver: runs as guest code over the SPU HLE",
    "libgs": "libgs internal (TMD object sort/preset, BG and clip setup): large or rarely called; guest code for now",
    "libgte": "not in PsyCross; an HLE on the gteRegs bank via the bridge is possible",
    "libgpu": "libgpu internal",
    "libcd": "CD: guest code over the CD controller HLE",
    "libetc": "libetc",
    "libapi": "BIOS calls: the BIOS HLE handles them",
    "libpad": "pad: guest code over the SIO HLE",
    "libmcrd": "memory card: guest code over the card HLE",
    "apicard": "memory card BIOS: card HLE",
    "libc2": "libc",
}


def lookup(name: str, segment: str | None) -> Status:
    if name in TABLE:
        return TABLE[name]
    for prefix, note in SEGMENT_NOTES.items():
        if segment and segment.startswith(prefix):
            return Status(S, note=note)
    return Status(S, note="not reviewed")
