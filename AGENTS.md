# AGENTS.md — Crimson Desert ESDF Mod & Unpacker Workflow

This repository (`crimson-desert-unpacker`) contains the PAZ/PAMT archive unpacker and the update scripts used to maintain the **Crimson Desert ESDF Keybinding Mod** (with German **QWERTZ** `Y` adjustments).

## Directory Layout

All related directories live side-by-side under `/home/g0ph3r/dev/crimson-desert/`:

```text
/home/g0ph3r/dev/crimson-desert/
├── crimson-desert-unpacker/          # This repository
│   ├── .env                          # Version configuration (PREVIOUS_VERSION, NEW_VERSION, paths)
│   ├── check_update.sh               # Unpacks inputmap*.xml from Steam PAMT & diffs against previous version
│   └── python/paz_unpack.py          # Python CLI unpacker
├── vanilla/                          # Extracted vanilla files per game version
│   ├── 2.05.00/ui/inputmap*.xml
│   └── 2.05.01/ui/inputmap*.xml
└── mod/                              # Packaged ESDF mod releases per version
    └── crimson-desert-esdf-<MOD_VER>-<GAME_VER>/
        ├── README.md                 # Documentation, changelog, and complete mapping reference
        ├── ui/                       # Modded XML files ready for the game folder
        │   ├── inputmap.xml
        │   └── inputmap_common.xml
        └── vanilla/                  # Vanilla reference files & unified diffs for this game version
            ├── inputmap.xml
            ├── inputmap.xml.diff
            ├── inputmap_common.xml
            └── inputmap_common.xml.diff
```

---

## Step-by-Step Guide: Updating the Mod for a New Game Patch

When Crimson Desert receives a game update, follow these steps to update the ESDF mod:

### 1. Check `.env` and Unpack Vanilla Files
1. Open [`.env`](file:///home/g0ph3r/dev/crimson-desert/crimson-desert-unpacker/.env) and verify `PREVIOUS_VERSION` (e.g., `"2.05.00"`) and `NEW_VERSION` (e.g., `"2.05.01"`).
2. If the user hasn't already run [`check_update.sh`](file:///home/g0ph3r/dev/crimson-desert/crimson-desert-unpacker/check_update.sh), execute it:
   ```bash
   ./check_update.sh
   ```
   This extracts `ui/inputmap.xml` and `ui/inputmap_common.xml` into `../vanilla/${NEW_VERSION}/` and compares them against `../vanilla/${PREVIOUS_VERSION}/`.

### 2. Analyze the Vanilla Diff
Compare the previous vanilla version with the new vanilla version:
```bash
diff -ru ../vanilla/<PREVIOUS_VERSION> ../vanilla/<NEW_VERSION>
```
Identify:
* Purely structural / gamepad changes (`<GamePad>`, `<SubGroupForPad>`, `LayerName`, `UIName`, etc.) → port directly into the modded XML files.
* New or modified `<KeyboardMouse Key="..." .../>` entries → port into the modded XML files **and apply the ESDF + QWERTZ mapping rules below**.

### 3. Create the New Mod Directory in `../mod/`
1. Copy the latest mod directory to the new target directory:
   ```bash
   cp -r ../mod/crimson-desert-esdf-<OLD_MOD_VER>-<PREV_GAME_VER> \
         ../mod/crimson-desert-esdf-<NEW_MOD_VER>-<NEW_GAME_VER>
   ```
2. Copy the new vanilla XML files into the new mod's `vanilla/` subfolder:
   ```bash
   cp ../vanilla/<NEW_GAME_VER>/ui/inputmap*.xml \
      ../mod/crimson-desert-esdf-<NEW_MOD_VER>-<NEW_GAME_VER>/vanilla/
   ```

### 4. Apply Changes to `ui/inputmap.xml` and `ui/inputmap_common.xml`
Apply all vanilla additions/changes from step 2 to `ui/inputmap.xml` and `ui/inputmap_common.xml` inside the new mod directory, translating any keyboard bindings according to the **ESDF Mapping Rules** below.

### 5. Regenerate Diffs & Update `README.md`
1. Inside `../mod/crimson-desert-esdf-<NEW_MOD_VER>-<NEW_GAME_VER>/vanilla/`, regenerate the unified diffs (relative paths `inputmap*.xml` vs `../ui/inputmap*.xml`):
   ```bash
   cd ../mod/crimson-desert-esdf-<NEW_MOD_VER>-<NEW_GAME_VER>/vanilla
   diff -u inputmap.xml ../ui/inputmap.xml > inputmap.xml.diff || true
   diff -u inputmap_common.xml ../ui/inputmap_common.xml > inputmap_common.xml.diff || true
   ```
2. Update `README.md` in the new mod directory:
   * Update the title header version.
   * Add a new changelog section at the top under `## Changelog` summarizing the merged vanilla changes and any new ESDF key remappings.

### 6. Verify File Encoding & Integrity (CRITICAL)
Before finishing, **always** verify both `ui/inputmap.xml` and `ui/inputmap_common.xml`:
* **UTF-8 BOM (`\xef\xbb\xbf`):** Both files **must** start with the 3-byte UTF-8 BOM. Without it, Crimson Desert fails to load `CursorModeOffKeyList` and character movement breaks.
* **CRLF Line Endings (`\r\n`):** Every line **must** end with `\r\n` (no bare `\n`).
* **`Name="OverrideKey"` Preservation:** Any `<Input>` referenced inside a `<SubGroup>` must keep `Name="OverrideKey"` (or `OverrideKey1`/`OverrideKey2`) on its `<KeyboardMouse>` elements, otherwise `<SubGroup>` initialization fails at startup.

Quick verification snippet:
```bash
python3 -c '
import pathlib
base = pathlib.Path("../mod/crimson-desert-esdf-<NEW_MOD_VER>-<NEW_GAME_VER>")
for f in ["ui/inputmap.xml", "ui/inputmap_common.xml"]:
    b = (base / f).read_bytes()
    assert b.startswith(b"\xef\xbb\xbf"), f"Missing BOM in {f}"
    assert b.count(b"\r\n") == b.count(b"\n"), f"Non-CRLF line endings in {f}"
    print(f"OK: {f} (size={len(b)}, lines={b.count(b\"\r\n\")})")
'
```

---

## Core ESDF + QWERTZ Mapping Rules

When new `<KeyboardMouse>` bindings appear in a patch, remap them consistently with the existing mod architecture (see the mod's `README.md` for full details):

| Vanilla Key(s) | Role / Context | ESDF Mod Key(s) | Notes |
| :--- | :--- | :--- | :--- |
| **`W, A, S, D`** | Directional movement, camera/map pan, menu grid navigation, 2-axis (`W/S` or `A/D`), QTE directions | **`E, S, D, F`** | Up = `E`, Left = `S`, Down = `D`, Right = `F` (exclude piano/musical scale keys `RecitalC3..C4` / `FreeRecitalC..B`) |
| **`Q / E`** (paired) | Paired left/right tab, sub-menu, rotation, zoom, scroll, FOV, option cycling, gimmick LT/RT | **`W / R`** | Left/Prev = `W`, Right/Next = `R` |
| **`E`** (single) | Primary interaction (`Key_Interaction`), menu confirm/select/equip, QTE/minigame primary button | **`A`** | Shifted left of `S` so `E` is free for Move Forward |
| **`F`** (single) | Secondary action, item usage (`UseItem*`), item inspect, secondary menu button | **`A`** or **`W`** | In menus/contexts where `W/R` is already used for tab/scroll/zoom (e.g. `UIMenuPanel`, `UIModalMessage`, `CookingPanel`, `ItemEnchantPanel`, `HorseStablePanel`, etc.), map `F` → **`A`**. In menus without `W/R` pairs (or QTE Y button), map `F` → **`W`**. For gameplay `FistAttack` / `Skill_4` / `Skill_17` / `GimmickInput_F`, see `README.md` (`T`, `G`, `W`, `A`). |
| **`R`** (single) | Reload / sub-attack / grapple / tertiary menu action (`buttonY` / toggle detail / world map actions) | **`Q`** | Shifted left of `W` so `R` is free for Right Tab / Gimmick Right. **Exception:** `<Input Name="InteractionA">` (`Interaction2` in `KeyGuidePanel` + `Key_SubInteraction*` / `Key_RideOff*` in `Cancel`) stays on **`R`** to avoid triggering `SubAttack` (`Q`) when talking to shopkeepers. |
| **`Z`** (with `C`) | Category / tab switching (`Z + C`), `Key_LeftStick` / `LeftStickDownOnce`, `CursorModeOffKeyList` | **`Y`** | Replaced `z` directly with `y` (single binding, not dual `z`+`y`) so German **QWERTZ** keyboards work seamlessly and in-game HUD prompts cleanly show `Y` instead of `Y/Z`. |
| **`Space`, `LShift`, `Alt`, `Ctrl`, `X`, `C`, `V`, `G`, `T`, `Tab`, `Esc`** | Jump/Ascend, Run/Accelerate, Evade/Descend, Lock-on, Crouch, etc. | **Unchanged** | Keep vanilla keys unless they specifically conflict with an ESDF key. |
