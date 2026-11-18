# HaGiangGTA — Đèo Mã Pí Lèng Open World (Unreal Engine 5.5)

24-hour challenge: one 4 km × 4 km landscape with three Hà Giang viewpoints
(Dốc Thẩm Mã → Mã Pí Lèng / Mỏm Đá Tử Thần → Hẻm vực Tu Sản & sông Nho Quế),
built **code-first**: every checkpoint branch can rebuild its whole scene from
C++ (`Source/`) plus a Python editor script (`Scripts/Checkpoints/build_cp0X.py`).

## Requirements
- Unreal Engine **5.5** (Epic Games Launcher)
- macOS: Xcode (Apple Silicon recommended — Nanite/Lumen need Metal SM6)
- Windows: Visual Studio 2022 with the "Game development with C++" workload

## Rebuilding a checkpoint
```bash
git checkout cp-01-10min            # or any checkpoint tag / branch
```
1. Right-click `HaGiangGTA.uproject` → **Generate Xcode / Visual Studio project files**,
   then open the `.uproject` and let UE compile the `HaGiangGTA` module.
2. In the editor: **Tools → Execute Python Script…** → pick
   `Scripts/Checkpoints/build_cp0X.py` for that checkpoint.
   (Or in the Output Log, switch the command box to **Python** and type the script's full path.)
3. The script creates/overwrites the checkpoint map under `/Game/Maps/` and
   saves it. Press **Play**.

Command-line alternative (macOS):
```bash
Scripts/run_checkpoint.sh 01
```

## Layout
| Path | What |
|---|---|
| `Source/HaGiangGTA/` | C++ gameplay module |
| `Scripts/Checkpoints/` | One rebuild script per checkpoint |
| `Scripts/hg_lib/` | Shared editor-Python helpers |
| `Scripts/Tools/work_timer.py` | Active-work stopwatch (`ACTIVE_WORK_TIMER.json`) |
| `Config/` | Engine/game/input config (TSR, Lumen, Nanite, streaming pool) |
| `CHECKPOINT_LOG.md` | What each checkpoint contains and how to test it |

`Content/Maps/`, generated heightmaps and Megascans/Fab packs are git-ignored:
they are rebuilt by the scripts or re-downloaded.
