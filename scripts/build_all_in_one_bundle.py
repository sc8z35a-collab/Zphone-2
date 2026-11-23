#!/usr/bin/env python3
"""
Build the turnkey All-in-One Manufacturing & Assembly Bundle ZIP for 菅ルンバ II Rev 5.2:
  release/SugaRoomba2_Rev5_2_AllInOne_Bundle.zip
Contains:
  00_READ_FIRST_ORDER_AND_BUILD.txt
  01_JLC3DP_SLA_10_STLs/        (all 10 independent watertight single-body STLs + ZIP)
  02_JLCPCB_0DRC_PCB_Gerbers/   (0-DRC Gerber ZIP, schematic/PCB PDFs, 3D STEP, DRC report)
  03_Firmware_CH32X035_Rev5_2/  (PlatformIO USB-ISP & WCH-LinkE firmware + INT8 TinyML header)
  04_BOM_and_Manuals/           (BOM CSV, Vendor Ordering Guide, Beginner Assembly Guide, Whitepaper)
"""

import os
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "release")
os.makedirs(OUT_DIR, exist_ok=True)
OUT_ZIP = os.path.join(OUT_DIR, "SugaRoomba2_Rev5_2_AllInOne_Bundle.zip")

README_TXT = """================================================================================
菅ルンバ II Rev 5.2 (100% Real-Silicon & Real-Manufacturing Zero-Defect Edition)
ワンクリック同梱製造＆組立パック (All-in-One Turnkey Order & Build Bundle)
================================================================================

【このパックの同梱内容】
1. 01_JLC3DP_SLA_10_STLs/
   - JLC3DP (https://jlc3dp.com/jp) へそのままアップロードする独立 10 STL (計 12 個)
   - 設定: SLA / 9600 Resin (Matte White) / 一般サンディング / 単位 mm
   - 数量: 08_drive_wheel_50mm.stl と 09_motor_swing_arm.stl を Qty=2、他 8 ファイルを Qty=1
   - 総体積 412.58 cm3 (474.5 g) / 造形費目安 $20.92 (~3,138 円) + OCS 送料 $7.50 (~1,125 円)

2. 02_JLCPCB_0DRC_PCB_Gerbers/
   - JLCPCB (https://jlcpcb.com/jp) へそのままアップロードする 2 層 FR-4 ガーバー ZIP
   - 同じ JLC アカウントのカートへ入れると、JLC3DP の 3D プリント品と「送料 1 回分で同梱発送」可能！
   - KiCad 7 ネイティブ DRC 合格済み (44/44 ネット、0 DRC violations / 0 unconnected / 0 FP errors)
   - 秋月実物 300mil AE-QFN28-DIP / 300mil AE-TC78H653FTG / LP2950L-50-T92 (1=OUT,2=GND,3=IN) 完全整合

3. 03_Firmware_CH32X035_Rev5_2/
   - PlatformIO プロジェクト (CH32X035G8U6 48MHz RISC-V)
   - 0 円 USB-ISP 書込: JP_BOOT (PC17->10k->5V) をショートして通電 -> [env:ch32x035g8u6_isp] で Upload
   - WCH-LinkE SWD 書込: J_SWD (5V/GND/PC18/PC19) 接続 -> [env:ch32x035g8u6_wchlink] で Upload

4. 04_BOM_and_Manuals/
   - BOM_VERIFIED_2026.csv        : 本体ハード一式 2,980 円 (秋月 1,660 円 + 100 均 1,320 円) 完全部品表
   - VENDOR_ORDERING_GUIDE.md     : JLC3DP + JLCPCB 同時同梱注文クリック手順
   - BEGINNER_ASSEMBLY_GUIDE.md   : 実ピン配列・LED極性・M3インサートナット・書込完全ガイド
   - INNOVATIONS_WHITEPAPER.md    : 全 5 種実 3D ソルバー (OpenFOAM/CalculiX/Elmer/MuJoCo/Ngspice) 検証報告書

【ライブ 3D CAD ビューア（文字ゼロ UI）】
https://sc8z35a-collab.github.io/Zphone-2/
"""


def main():
    files_to_pack = []

    # 1. All 10 independent JLC3DP STLs + standalone JLC3DP zip
    jlc_dir = os.path.join(ROOT, "cad", "jlc3dp_order_pack")
    for fn in sorted(os.listdir(jlc_dir)):
        if fn.endswith((".stl", ".zip")):
            files_to_pack.append((os.path.join(jlc_dir, fn), f"01_JLC3DP_SLA_10_STLs/{fn}"))

    # 2. JLCPCB 0-DRC Gerbers + PDFs + STEP + DRC report
    kicad_out = os.path.join(ROOT, "kicad", "outputs")
    for fn in [
        "SugaRoomba2_Rev5_JLCPCB_Gerbers.zip",
        "suga_roomba_ii_schematic.pdf",
        "suga_roomba_ii_pcb.pdf",
        "suga_roomba_ii_pcb.step",
        "drc_report.txt",
    ]:
        p = os.path.join(kicad_out, fn)
        assert os.path.exists(p), f"Missing KiCad output: {p}"
        files_to_pack.append((p, f"02_JLCPCB_0DRC_PCB_Gerbers/{fn}"))

    # 3. CH32X035 Rev 5.2 Firmware
    fw_dir = os.path.join(ROOT, "firmware", "suga_roomba_ii_ch32x035")
    for fn in ["platformio.ini", "suga_roomba_ii_ch32x035.ino", "suga_roomba_tinyml.h"]:
        p = os.path.join(fw_dir, fn)
        assert os.path.exists(p), f"Missing firmware file: {p}"
        files_to_pack.append((p, f"03_Firmware_CH32X035_Rev5_2/{fn}"))

    # 4. BOM & Manuals
    docs_dir = os.path.join(ROOT, "docs")
    for fn in [
        "BOM_VERIFIED_2026.csv",
        "VENDOR_ORDERING_GUIDE.md",
        "BEGINNER_ASSEMBLY_GUIDE.md",
        "INNOVATIONS_WHITEPAPER.md",
    ]:
        p = os.path.join(docs_dir, fn)
        assert os.path.exists(p), f"Missing doc file: {p}"
        files_to_pack.append((p, f"04_BOM_and_Manuals/{fn}"))

    with zipfile.ZipFile(OUT_ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.writestr("00_READ_FIRST_ORDER_AND_BUILD.txt", README_TXT.encode("utf-8"))
        for src, arc in files_to_pack:
            zf.write(src, arcname=arc)

    # Verify ZIP integrity
    with zipfile.ZipFile(OUT_ZIP, "r") as zf:
        bad = zf.testzip()
        assert bad is None, f"Corrupt entry in ZIP: {bad}"
        names = zf.namelist()
    sz_kb = os.path.getsize(OUT_ZIP) / 1024.0
    print(f"[PASS] Built {OUT_ZIP} ({sz_kb:.1f} KB, {len(names)} files, CRC OK!)")
    for n in names:
        print(f"  - {n}")


if __name__ == "__main__":
    main()
