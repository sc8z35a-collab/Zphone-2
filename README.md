# 菅ルンバ II — Rev 5.0（Production-Ready Real-Hardware Edition）
**机上の誇張スペックを全廃し、「このまま JLC3DP・秋月電子・100均で発注／購入して組み立てれば、3D干渉ゼロ（`0.00 mm³`）・気流漏れゼロ・DFM一発合格で確実に組めて・吸って・走る実機」へ全面再設計した完全自作ロボット掃除機**

> **Rev 4.0（机上理想モデル）→ Rev 5.0（実機確実動作版）の全面方針転換**  
> 過去の誇張数値（3,033 Pa・ファン効率 82.6%・2,939 G・48.2 dBA・全高 52 mm・肉厚 0.85 mm・3パーツランナー一括・ネジ0本・50 µs Back-EMF）を **すべて潔く廃止** しました。  
> 代わりに、**JLC3DP SLA `9600 Resin` 実公差（±0.15 mm）・基本肉厚 1.6〜2.0 mm・独立10STL（ランナー0）・実部品データシート寸法（`RS-385PH` Ø27.5×37.8mm＋端子 / `PWN10EB12CB` 25.1×20.0×14.9mm）・M3ネジ統一・1.5 ms Back-EMF窓** へ全面改修し、**4つの自動物理監査（全105ペア3D体積干渉=0.00 mm³・0.01 mm隙間監査21/21 PASS・3Dボクセル気流貫通＆床漏れ0・BOM↔STL 100%一致）に全項目合格** した実機図面です。

![菅ルンバ II Rev 5.0 ゼロテキスト 3D CAD ビューア（アイソメ図）](docs/images/cad_01_iso.png)

---

## 初心者の方へ：まず開くべき 4 つのワンクリック入口

1. **🖱️ ブラウザで今すぐグルグル回せる「文字ゼロ」ライブ 3D CAD ビューア（Rev 5.0 実機モデル）**  
   → 🌐 **ライブ版（GitHub Pages）**：[**https://sc8z35a-collab.github.io/Zphone-2/**](https://sc8z35a-collab.github.io/Zphone-2/)  
   → 💻 **ローカル版**：[`index.html`](index.html) または [`cad-viewer/index.html`](cad-viewer/index.html)
2. **🏭 3Dプリント業者（JLC3DP）独立10パーツ発注＆基板同梱マニュアル（送料別・送料込み実見積り付）**  
   → 📖 注文画面クリック順マニュアル：[`docs/VENDOR_ORDERING_GUIDE.md`](docs/VENDOR_ORDERING_GUIDE.md)  
   → 📦 JLC3DP 即アップロード用 独立10STL一括ZIP：[`cad/jlc3dp_order_pack/SugaRoomba2_Rev5_JLC3DP_Pack.zip`](cad/jlc3dp_order_pack/SugaRoomba2_Rev5_JLC3DP_Pack.zip)  
   → 📦 JLCPCB 専用2層PCBガーバーZIP（任意）：[`kicad/outputs/SugaRoomba2_Rev5_JLCPCB_Gerbers.zip`](kicad/outputs/SugaRoomba2_Rev5_JLCPCB_Gerbers.zip)
3. **📖 初心者でもM3ドライバー1本で確実に組める完全組立ガイド（欠品ゼロ完全部品表つき）**  
   → [`docs/BEGINNER_ASSEMBLY_GUIDE.md`](docs/BEGINNER_ASSEMBLY_GUIDE.md) / [`docs/BOM_VERIFIED_2026.csv`](docs/BOM_VERIFIED_2026.csv)
4. **🚀 Google Colab A100 実3D物理シミュレーション＆実機検証ラボ（環境分離・4フェーズ構成）**  
   1D近似を廃止し、実3Dソルバー（**OpenFOAM v2406 / Autodesk XLB+Warp / CalculiX 2.21 / Elmer FEM / j-Wave / MuJoCo+MJX / Genesis / Ngspice 42 200回MC / KiCad CLI**）を **Google Drive 永続化・ヘッドレス描画・2段階メッシュ/時間刻み収束（`< 5%`）** 付きでワンクリック再現できます：

   | Phase | 対象カテゴリ（環境分離） | 導入ソルバー（GPU高速探索＋CPU最終検証） | Google Colab A100 ワンクリック起動 |
   |---|---|---|---|
   | **Phase 1** | **流体 CFD・回転翼・混相流** | **OpenFOAM v2406 MPI** (`simpleFoam`+MRF+`kOmegaSST`) + **Autodesk XLB / NVIDIA Warp GPU** + PyFR | [![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/sc8z35a-collab/Zphone-2/blob/main/colab/01_Fluid_CFD_OpenFOAM_XLB_A100.ipynb) |
   | **Phase 2** | **構造 FEM・固有振動・3D音響** | **CalculiX 2.21 (`C3D10`)** + **Elmer FEM (`HelmholtzSolver`)** + **j-Wave GPU** (+任意 FEniCSx/OpenRadioss) | [![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/sc8z35a-collab/Zphone-2/blob/main/colab/02_Struct_Acoustic_FEM_CalculiX_Elmer_A100.ipynb) |
   | **Phase 3** | **摩擦接触・段差/登坂・粉体MPM・RL** | **MuJoCo 4.x** + **MuJoCo MJX (A100 1,024並列)** + **Genesis GPU MPM 粉体清掃** (`show_viewer=False`) | [![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/sc8z35a-collab/Zphone-2/blob/main/colab/03_Robotics_MuJoCo_MJX_Genesis_A100.ipynb) |
   | **Phase 4** | **0.01mm干渉・SPICE MC・KiCad CLI** | `manifold3d` 105ペア + 2段階3Dボクセル + **Ngspice 42 200回MC** + **KiCad CLI** DRC/ガーバー/STEP | [![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/sc8z35a-collab/Zphone-2/blob/main/colab/04_CAD_Circuit_KiCad_Audit_A100.ipynb) |
   | *(旧統合)* | TinyML (INT8 952B) 学習＆SLAM | `CH32X035` INT8 推論ヘッダ生成＆80mmソファ下SLAM | [![Open In Colab](https://colab.research.google.com/assets/colab-badge.svg)](https://colab.research.google.com/github/sc8z35a-collab/Zphone-2/blob/main/colab/SugaRoomba2_AI_CFD_RL_Lab.ipynb) |

---

## Rev 4.0（机上モデル）→ Rev 5.0（実3D物理ソルバー検証確定版）の誠実な仕様比較

| 評価指標 | Rev 4.0（旧・誇張／実機組立不可） | **Rev 5.0 実3Dソルバー確定仕様（OpenFOAM / CalculiX / Elmer / MuJoCo / Ngspice）** | 市販 ¥20,000 入門機 |
|---|---|---|---|
| **外形（W × L × H）** | 196 × 188 × 52.0 mm *(RS-385PHが収まらない)* | **216.0 × 219.2 × 75.6 mm**（**主甲板 64.0 mm**＋中央モータードーム 75.6 mm／**端子未曲げ＋配線余裕 +3.70 mm**） | Ø300〜330 × 82〜92 mm |
| **実測総質量 / 左右重心 X** | 405.1 g *(車輪・ネジ・肉厚過小)* | **797.5 g**（SLA 12個 476.8 g＋電子/金属/電池 320.7 g）／ **CoG X = +0.09 mm**（MuJoCo 3D検証 駆動輪荷重 **84.66%**） | 2,800〜3,500 g |
| **SLA造形・DFM仕様** | 肉厚 0.85 mm・3パーツランナー結合・密閉インペラ | **独立10STL（計12個・ランナー0）・壁厚 1.6〜2.0 mm・羽根厚 1.25〜1.35 mm・全10個水密＆単一ソリッド** | 射出成形 |
| **実3D締切吸引力（OpenFOAM v2406）** | ~~2,360 / 3,033 Pa~~ *(誇張)* | **1,123 Pa（標準 6.0V・189kセル 2段階収束 3.03%） / 1,330 Pa（満充電 6.6V ターボ）**（※1Dオイラー理論上限 1,294/1,576 Pa・実運転平衡 680〜850 Pa @ 1.5〜2.0 L/s） | 1,500〜2,000 Pa |
| **実3D構造＆固有振動（CalculiX `C3D10`）** | ~~SLA一体板バネ常時27 MPa~~ *(クリープ割れ)* | **インペラ 12,100 rpm：6.38 MPa（安全率 7.5x）・遠心撓み 0.47 mm `< 0.90 mm`隙間・`f1 = 325.4 Hz`（収束 0.15%）／ 揺動アーム 3.2 N：1.40 MPa（対クリープ安全率 10.0x！）** | 射出成形＋金属バネ |
| **実3D消音器（Elmer FEM `UMFPACK`）** | ~~48.2 dBA~~ *(非現実)* | **実3D共鳴 Peak TL = 35.6 dB @ 2,000 Hz（2段階収束 4.52%）**（※1D予測 1,480 Hz との差を3D発見・実測想定 ~60〜62 dBA。Rev 5.1 でネック `Ø5.6 mm` 化すれば `1,499 Hz` BPF1 直撃同調可！） | 62〜66 dBA |
| **実3D走行・登坂・段差（MuJoCo 4.0.1）** | *(1D 34.6° / 7.5mm 楽観推定)* | **巡航 0.271 m/s（収束 0.000%）・実3D登坂限界 22°（重心荷重移動反映）・剛体段差限界 ≤3.0 mm（3.5 mm 前面スカート支配）・バンパー 2.38 mm 硬質ストッパー確認** | 15°〜20° / 10〜15 mm |
| **空気流路の完全連続性（2段階3Dボクセル）** | ~~ブロワー直下に床貫通穴＋13mm段差断絶~~ | **Coarse 363k vs Fine 781k ボクセル収束 3.58%（145.9 vs 151.3 cm³）・床漏れ 0/1,768・経由点 13/13・電装室漏れ 0/3** | 連続流路 |
| **Back-EMF 仮想気圧計（Ngspice 200回MC）** | ~~50 µs~~ *(還流電流が残る危険あり)* | **1.5 ms PWM-OFF窓・200回モンテカルロ（5.4–6.7V / R±5% / C±20% / L=300–600µH）：200/200 PASS（5.0V最悪 5.000 V・ADC最悪 3.45 V・残留電流最悪 0.052 mA）** | 上位機のみ |
| **実コスト（正直な両表記）** | ~~¥2,880~~ *(車輪/ネジ/送料漏れ・体積過小)* | **電子＋100均機構全品 ¥2,630** ＋ **JLC3DP 10STL（414.7 cm³）¥3,153（送料別）/ ¥4,278（OCS送料$7.50込）** → **フルSLA合計 ¥5,783（送料別）/ ¥6,908（送料込）**（※主要流体コアのみSLA＋PP板ハイブリッドなら **約¥2,922〜¥3,020** も選択可！） | ¥19,800〜¥24,800 |

---

## 実3D物理ソルバー 2段階収束検証サマリー（[`simulations/real3d/`](simulations/real3d/)）

1D近似式ではなく、産業標準の **実3Dソルバー5種（OpenFOAM v2406 MPI / CalculiX 2.21 / Elmer FEM / MuJoCo 4.0.1 / Ngspice 42 200回MC）** を Rev 5.0 実STL・実回路へ直接適用し、全項目で **Coarse vs Fine の2段階収束（`< 5%`）** と物理保存則を監査しました。

| 実3Dソルバー | 検証対象（実STL / 実回路） | Coarse vs Fine 2段階収束結果 | 実3Dソルバーが暴いた重要設計知見（Rev 5.1 反映指針） | 生データ |
|---|---|---|---|---|
| **OpenFOAM v2406 MPI** (`simpleFoam` + MRF + `kOmegaSST`) | `05_semi_open_impeller.stl`（軸0.90/径2.05mm隙間＋CADディフューザー） | **6.0V 締切 `1,089.3` vs `1,123.3 Pa`（`3.03% < 5%` PASS）**・質量保存誤差 `0.018–0.474%`・6.6V `1,330.3 Pa`（相似則 `0.31%`）・軸トルク収束 `4.79%` | 同心円ボリュートの高流量旋回損失により `3.0 L/s` では `345 Pa` へ低下（実平衡点は `1.5–2.0 L/s` で `680–850 Pa`）。Rev 5.1 で **真の対数渦巻スクロール化** が最大の空力改善点。 | [`01_openfoam_3d_cfd_summary.json`](simulations/real3d/results/01_openfoam_3d_cfd_summary.json) |
| **CalculiX 2.21 (`C3D10`)** + Gmsh | `05_semi_open_impeller.stl` (12,100 rpm) & `09_motor_swing_arm.stl` (3.2 N) | インペラ変位 `0.475` vs `0.472 mm`（**`0.65%`**）・`f1` `325.9` vs `325.4 Hz`（**`0.15%`**）・p99.5応力 **`1.76%`**・アーム変位 **`0.69%`** | インペラ遠心撓み `0.47 mm` は `0.90 mm` 隙間に安全収容（旧 Rev 4.0 の `0.35 mm` では確実に接触破損していた！）。揺動アーム `1.40 MPa << 14 MPa` クリープ限界（`SF=10.0x`）。 | [`02_calculix_elmer_3d_fem_summary.json`](simulations/real3d/results/02_calculix_elmer_3d_fem_summary.json) |
| **Elmer FEM 26.2** (`HelmholtzSolver` + `UMFPACK`) | 実3D後部排気コレクタ＋双方向ヘルムホルツ空洞（直管校正 `0.0006 dB`） | **Peak TL `34.00` vs `35.61 dB @ 2,000 Hz`（`4.52% < 5%` PASS）**・`TL@1499Hz` `1.67` vs `1.68 dB`（**`0.60%`**） | 1D計算 `1,480 Hz` に対し実3D空洞は **`2,000 Hz` 共鳴** を発見。Colab NB #2 にて `Ø5.6 mm` ネック縮小で `1,499 Hz` BPF1 へ直撃同調できることを実証。 | [`02_calculix_elmer_3d_fem_summary.json`](simulations/real3d/results/02_calculix_elmer_3d_fem_summary.json) |
| **MuJoCo 4.0.1** (3D剛体接触・$\mu=0.82$) | 797.5g 実重心・`PWN10EB12CB` 実トルク・3.2 N 輪ゴムスリップ上限・バンパー | `dt=2.0ms` vs `0.5ms`：**`vx = 0.2714 m/s`（`0.000%`）・駆動輪荷重 `84.66%`（`0.000%`）** | 3D重心移動により実登坂限界は 1D `34.6°` → **`22°`**（`25°` で失速）。剛体段差は前面 `3.5 mm` スカート支配で **`≤ 3.0 mm`** 通過（柔軟ラグは圧縮通過）。 | [`03_mujoco_3d_contact_summary.json`](simulations/real3d/results/03_mujoco_3d_contact_summary.json) |
| **2段階3Dボクセル + Ngspice 42 200回MC + KiCad** | 全10STL気流空間 + 電源/抵抗/電解/Lばらつき 200コーナー + 2層PCB | ボクセル `145.88` vs `151.29 cm³`（**`3.58% < 5%` PASS**）・MC `200/200` 5V無瞬断・ADC `<3.45V`・残留 `<0.052mA` | ±5% 炭素皮膜抵抗（R18/R19）のDC偏差（中央値 `230 mV`）は、起動時開放基準差分（$\Delta V_{\text{bemf}}$）または 1% 金属皮膜抵抗で完全相殺すべきことを実証。 | [`04_cad_spice_mc_summary.json`](simulations/real3d/results/04_cad_spice_mc_summary.json) |

---

## 4大自動物理監査の完全PASSエビデンス（[`cad/verify_assembly_physics.py`](cad/verify_assembly_physics.py)）

単なる `is_watertight` 確認ではなく、コンパイル済み全STLとオフザシェルフ実部品3Dモデルに対し、以下4つの自動検証を実行し **全項目 PASS（`exit=0`）** を証明しました（生データ：[`cad/assembly_physics_audit.json`](cad/assembly_physics_audit.json)）。

1. **全ペア exact 3D Boolean 体積干渉テスト（`manifold3d`）**：  
   筐体・ボリュート蓋・サイクロン蓋・ダストカップ・インペラ・キャノピー・バンパー・車輪×2・揺動アーム×2・アジテーター・`RS-385PH`・駆動モーター×2＆M3車軸/竹串・単3電池×5・メインPCB・センサー＆16mmビー玉の **全105ペアすべてで 3D交差体積 `0.000 mm³`（干渉 0 件）**。
2. **0.01 mm 単位 最小クリアランス監査表（全21箇所 PASS・SLA公差 ±0.15 mm 以上確保）**：  
   - インペラ外周↔ディフューザー翼：**`2.05 mm`**（基準 ≥1.50 mm）
   - インペラ下端↔ボリュート床 / 上端↔ボリュート蓋：**`0.90 mm` / `0.90 mm`**（基準 ≥0.80 mm）
   - インペラ軸穴（Ø2.45）↔ `RS-385PH` 軸（Ø2.30）：**`0.07 mm`** 片側（接着スリップフィット）
   - 駆動車輪↔ホイールハウス / キャノピー：**`2.00 mm` / `5.52 mm`**
   - 前面アジテーター↔ノズル壁：**`1.60 mm`**
   - 着脱ダストカップ↔サイクロン筒 / 蓋：**`0.40 mm` / `0.40 mm`**
   - フローティングバンパー↔筐体＆ガイド溝：**`0.50 mm`**
   - モーター揺動アーム↔ヒンジクレビス：**`0.55 mm`**
   - `RS-385PH` 未曲げ端子↔キャノピードーム内天井：**`3.70 mm`**（配線曲げ余裕十分！）
   - `RS-385PH` スリーブ / 胴体↔メインPCB端：**`2.26 mm` / `4.81 mm`**
   - 単3電池×5↔ボリュート蓋 / メインPCB / 電池枠：**`2.90 mm` / `3.40 mm` / `0.30 mm`**
   - メインPCB＆470µF電解コンデンサ天端↔キャノピー天井：**`2.30 mm`**
3. **2段階3Dボクセル BFS フラッドフィル流路貫通＆床バイパス漏れゼロテスト（363k vs 781k ボクセル・収束差 3.58%）**：  
   - ブロワー直下の床貫通チェック：**`1,768 / 1,768` 列すべて固体シール（床漏れ = `0`）**
   - 密閉内部流路の連結体積：**`151.3 cm³`** — 前面吸込口（`01`）から左右後部排気口（`12`, `13`）まで **13/13 ウェイポイントすべて単一連続空洞として貫通 PASS**
   - 左右電池室・光学マウス室への漏れプローブ：**3/3 完全密閉 PASS**
4. **組立挿入パス＆BOM↔STL 100% 突合チェック**：  
   - 上面開放ボリュート（ID 90 mm ← インペラ Ø76 mm）・上抜きダストカップ・電池→PCB→キャノピーの順組など **10/10 組立パス PASS**
   - 独立10STLすべて `watertight=True, bodies=1`、および [`docs/BOM_VERIFIED_2026.csv`](docs/BOM_VERIFIED_2026.csv) との **100% 一致（欠品 0 件）PASS**

---

## 設計・実3D物理検証ギャラリー（すべて Rev 5.0 実STL・実ソルバー出力）

| OpenFOAM v2406 MPI 実3D MRF CFD（189kセル・締切 1,123/1,330 Pa・収束 3.03%） | CalculiX 2.21 (`C3D10`) 実3D構造/固有振動＋Elmer FEM 3D音響透過損失 |
|---|---|
| ![OpenFOAM v2406 実3D CFD](simulations/real3d/results/01_openfoam_3d_cfd_verification.png) | ![CalculiX＋Elmer 実3D FEM](simulations/real3d/results/02_calculix_elmer_3d_fem_verification.png) |

| MuJoCo 4.0.1 実3D剛体接触・登坂22°・段差・バンパー衝突（収束 0.000%） | Ngspice 42 200回モンテカルロ（5V無瞬断・1.5ms還流完了・R18/R19公差監査） |
|---|---|
| ![MuJoCo 4.0.1 実3D接触](simulations/real3d/results/03_mujoco_3d_contact_verification.png) | ![Ngspice 42 200回MC](simulations/real3d/results/04_ngspice_200run_montecarlo.png) |

| 3D CAD リアルタイム断面＆連続密閉流路（床漏れゼロ実証！） | 3D CAD 分解図（独立10STL＋全実部品・文字ゼロUI） |
|---|---|
| ![3D CAD 断面＆密閉流路](docs/images/cad_03_section_airflow.png) | ![3D CAD 分解図](docs/images/cad_02_exploded.png) |

| 3D CAD 底面（ブロワー直下完全密閉床・108mm口・16mmビー玉） | KiCad 7 Rev 5.0 2層PCB（59.7×55.9mm・31/31ネット・DRC=0） |
|---|---|
| ![3D CAD 底面](docs/images/cad_04_bottom_mechanics.png) | ![KiCad Rev 5.0 PCB](docs/images/kicad_pcb_layout.png) |

| 1D基準 P-Q・単筒サイクロン・ゴミ吸引（2.4倍）・Rev 5.0 実コスト監査 | Google Colab 仮想リビング清掃（80mmローソファ下進入SLAM） |
|---|---|
| ![Rev 5.0 実機監査](simulations/results/05_rev5_real_hardware_audit.png) | ![Colab SLAM](simulations/results/colab_03_slam_benchmark.png) |

---

## リポジトリ構成

```text
Zphone-2/
├── index.html                                  # 【GitHub Pages直結】厳格「文字ゼロUI」ライブ3D CADビューア
├── .nojekyll                                   # GitHub Pages 静的配信有効化
├── cad/
│   ├── suga_roomba_ii.scad                     # Rev 5.0 OpenSCAD パラメトリック3D CAD（肉厚>=1.6mm・独立10STL）
│   ├── export_parts.py                         # 29ターゲット並列コンパイル＋trimesh体積/重心/JLC3DP送料込見積
│   ├── verify_assembly_physics.py              # 【4大自動物理監査】全105ペアBoolean・0.01mm隙間・3DボクセルBFS・BOM突合
│   ├── cad_verification.json                   # 外形216×219×75.6mm・実質量797.5g・CoG X=+0.09mm・全水密検証結果
│   ├── assembly_physics_audit.json             # 全4テスト100% PASS生監査レポートJSON
│   ├── jlc3dp_order_pack/                      # 【業者即発注パック】独立10STL（ランナー0・水密・単一ソリッド）＋ZIP
│   │   ├── SugaRoomba2_Rev5_JLC3DP_Pack.zip    # JLC3DPへそのままアップロードできる独立10STL一括ZIP！
│   │   ├── 01_lower_chassis_unibody.stl        # 下部モノコック＋密閉床下ダクト＋下部サイクロン＋ボリュート＋7枚翼＋消音器
│   │   ├── 02_volute_lid_motor_mount.stl       # 4×M3着脱ボリュート蓋＋RS-385PH M3スリットクランプ
│   │   ├── 03_cyclone_cap_downcomer.stl        # 2×M3着脱サイクロン蓋＋ボルテックスファインダー＋上部リターンアーチ
│   │   ├── 04_removable_dust_cup.stl           # 上抜き清掃式ダストカップ（キー付き接線吸気口）
│   │   ├── 05_semi_open_impeller.stl           # Ø76mm 9枚セミオープン後退翼（内部サポート残留ゼロ）
│   │   ├── 06_upper_aero_canopy.stl            # Z=34mm密着4×M3キャノピー＋中央RS-385PHドーム（H=64/75.6mm）
│   │   ├── 07_front_bumper.stl                 # ツインスライドガイド式フローティングDバンパー
│   │   ├── 08_drive_wheel_50mm.stl             # Ø48mm SLA車輪リム（×2発注・輪ゴム装着でØ50mm）
│   │   ├── 09_motor_swing_arm.stl              # M3支点PWN10EB12CB揺動アーム（×2発注・輪ゴム張力・クリープ0）
│   │   └── 10_agitator_roller.stl              # 104mm リブ付き前面床アジテーター（Ø3mm竹串軸）
│   ├── stl/                                    # 12可視化STL + 全体アセンブリSTL
│   └── patterns/                               # 【超低予算ハイブリッド派用】1:1実寸PPシート＆アルミ缶型紙
├── cad-viewer/
│   ├── index.html                              # 厳格「文字ゼロUI」WebGL2 3D CADビューア（Playwright監査PASS）
│   └── stl_bundle.js                           # Rev 5.0 OpenSCAD実コンパイル済み3Dメッシュバンドル
├── kicad/
│   ├── generate_kicad.py                       # Rev 5.0 KiCad 7 回路図・59.7×55.9mm 2層PCB自動配線＆ネイティブDRC
│   ├── suga_roomba_ii.{kicad_pro,sch,pcb}      # KiCad 7 プロジェクト（31/31ネット・DRC=0・R18/R19統合）
│   └── outputs/
│       ├── SugaRoomba2_Rev5_JLCPCB_Gerbers.zip # JLCPCB 即発注用ガーバー＆ドリルZIP（DRC=0）
│       └── ...                                 # 回路図PDF/SVG、PCB PDF/SVG、3D STEP、DRCレポート
├── simulations/
│   ├── real3d/                                 # 【実3D物理ソルバー検証スイート（2段階収束 <5%）】
│   │   ├── run_01_openfoam_cfd.py              # OpenFOAM v2406 MPI MRF+kOmegaSST (115k vs 189k 格子独立性)
│   │   ├── run_02_calculix_elmer_fem.py        # CalculiX 2.21 C3D10 構造/固有振動 + Elmer FEM 3DヘルムホルツTL
│   │   ├── run_03_mujoco_contact.py            # MuJoCo 4.0.1 3D剛体接触・登坂22°・段差・バンパー衝突
│   │   ├── run_04_cad_spice_mc_audit.py        # 2段階3Dボクセル気流収束 + Ngspice 42 200回モンテカルロ + KiCad DRC
│   │   └── results/                            # 実3Dソルバー検証グラフPNG・生JSON（01〜04）
│   ├── run_all_validations.py                  # Rev 5.0 基準解析・単筒サイクロン・実コスト監査
│   ├── run_spice_validations.py                # ngspice 42 過渡解析（L=450µH・1.5ms Back-EMF還流完了実証）
│   ├── spice/*.cir                             # 4種のSPICEネットリスト
│   └── results/                                # 基準検証グラフPNG・JSON（01〜05 & colab_01〜03）
├── colab/
│   ├── 01_Fluid_CFD_OpenFOAM_XLB_A100.ipynb          # Phase 1: OpenFOAM v2406 + Autodesk XLB/Warp GPU LBM + PyFR
│   ├── 02_Struct_Acoustic_FEM_CalculiX_Elmer_A100.ipynb # Phase 2: CalculiX C3D10 + Elmer 3D音響 + j-Wave GPU
│   ├── 03_Robotics_MuJoCo_MJX_Genesis_A100.ipynb     # Phase 3: MuJoCo + MJX 1,024並列GPU + Genesis MPM粉体清掃
│   ├── 04_CAD_Circuit_KiCad_Audit_A100.ipynb         # Phase 4: 0.01mm干渉 + 2段階ボクセル + Ngspice 200回MC + KiCad CLI
│   ├── build_a100_notebooks.py                       # 上記4本のA100ノートブック自動生成＆AST構文検証スクリプト
│   ├── SugaRoomba2_AI_CFD_RL_Lab.ipynb               # TinyML 952B INT8 学習＆80mmソファ下SLAMノートブック
│   └── build_notebook.py                             # TinyML/SLAMノートブック生成スクリプト
├── firmware/
│   ├── suga_roomba_ii_ch32x035/                # 【Rev 5.0 標準】CH32X035G8U6 48MHz RISC-V ファームウェア
│   │   ├── suga_roomba_ii_ch32x035.ino         # 1.5ms Back-EMF仮想気圧計・1,123/1,330Pa制御・SLAM・INT8 TinyML
│   │   ├── suga_roomba_tinyml.h                # Colab学習済 952B Calibrated INT8 Cヘッダ（GCC -Wall -Wextra 合格）
│   │   └── platformio.ini                      # wchisp ネイティブUSB書込設定
│   └── suga_roomba_ii_rp2040/                  # レガシー RP2040 互換コード
└── docs/
    ├── BOM_VERIFIED_2026.csv                   # 欠品ゼロ完全部品表（10STL＋秋月16種＋100均7種＋OCS送料別/込両記）
    ├── VENDOR_ORDERING_GUIDE.md                # JLC3DP 独立10STL発注ガイド（送料別・送料込み実見積＋予算3000円案）
    ├── BEGINNER_ASSEMBLY_GUIDE.md              # M3ネジ統一・一発確実組立＆USB書込完全ガイド（Rev 5.0）
    ├── INNOVATIONS_WHITEPAPER.md               # Rev 4.0→5.0 物理バグ全修正＆実3Dソルバー検証ホワイトペーパー
    └── images/                                 # 3D CAD・KiCad実レンダリング画像一式
```