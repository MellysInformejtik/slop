#!/usr/bin/env python3
"""negroni (SM8450) port of UniXNU's NativeUfs, which is written for sheng (SM8550).

Applied by CI to the UniXNU checkout's drivers/NativeUfs before the build. Sources: Linux v6.12
arch/arm64/boot/dts/qcom/sm8450.dtsi (ufshc@1d84000, iommus = <&apps_smmu 0xe0 0x0>, power-domains
UFS_PHY_GDSC) and drivers/clk/qcom/gcc-sm8450.c (UFS branches at 0x87xxx; no UFS_MEM_PHY GDSC).
The host controller (0x1d84000) and apps_smmu (0x15000000) are at the same addresses as on SM8550.

usage: patch-native-ufs.py UNIXNU/drivers/NativeUfs
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])

GCC = {
    'GCC_UFS_PHY_GDSCR             0x77004': 'GCC_UFS_PHY_GDSCR             0x87004',
    # SM8450 has no separate UFS MEM PHY GDSC: point the read-only report at the UFS PHY GDSC
    'GCC_UFS_MEM_PHY_GDSCR         0x9e000': 'GCC_UFS_MEM_PHY_GDSCR         0x87004',
    'GCC_UFS_PHY_AXI_ENABLE        0x77018': 'GCC_UFS_PHY_AXI_ENABLE        0x87018',
    'GCC_UFS_PHY_AHB_ENABLE        0x77024': 'GCC_UFS_PHY_AHB_ENABLE        0x87020',
    'GCC_UFS_PHY_TX_SYMBOL0_ENABLE 0x77028': 'GCC_UFS_PHY_TX_SYMBOL0_ENABLE 0x87024',
    'GCC_UFS_PHY_RX_SYMBOL0_ENABLE 0x7702c': 'GCC_UFS_PHY_RX_SYMBOL0_ENABLE 0x87028',
    'GCC_UFS_PHY_UNIPRO_CORE_ENABLE 0x77068': 'GCC_UFS_PHY_UNIPRO_CORE_ENABLE 0x87064',
    'GCC_UFS_PHY_ICE_CORE_ENABLE   0x77074': 'GCC_UFS_PHY_ICE_CORE_ENABLE   0x8706c',
    'GCC_UFS_PHY_PHY_AUX_ENABLE    0x770b0': 'GCC_UFS_PHY_PHY_AUX_ENABLE    0x870a4',
    'GCC_UFS_PHY_RX_SYMBOL1_ENABLE 0x770cc': 'GCC_UFS_PHY_RX_SYMBOL1_ENABLE 0x870c0',
    'GCC_AGGRE_UFS_PHY_AXI_ENABLE  0x770e4': 'GCC_AGGRE_UFS_PHY_AXI_ENABLE  0x870d4',
}

CPP = {
    # the negroni ADT's ufshc node says qcom,sm8450-ufshc; without this the driver never attaches
    '"qcom,sm8550-ufshc"': '"qcom,sm8450-ufshc"',
    # GCC UFS block window 0x77000 -> 0x87000, MEM PHY GDSC page -> the UFS PHY GDSC page
    'GCC_BASE + 0x77000': 'GCC_BASE + 0x87000',
    'GCC_BASE + 0x9e000': 'GCC_BASE + 0x87000',
    '- 0x77000)': '- 0x87000)',
    # UFS stream ID 0x60 (SM8550) -> 0xe0 (SM8450)
    'smmuBypassStream (0x60,': 'smmuBypassStream (0xe0,',
    '(smr ^ 0x60U)': '(smr ^ 0xe0U)',
}


def patch(path, table):
    text = path.read_text()
    for old, new in table.items():
        if old not in text:
            raise SystemExit(f'{path.name}: {old!r} not found (NativeUfs changed upstream)')
        text = text.replace(old, new)
    path.write_text(text)
    print(f'patched {path.name}: {len(table)} SM8450 substitutions')


patch(root / 'GccUfs.h', GCC)
patch(root / 'NativeUfs.cpp', CPP)
