![Gaea2Unreal](screenshots/gaea2Unreal.jpg)

# Gaea2Unreal (UE 5.7 Compatible)

Use Gaea2Unreal, specifically the Gaea Landscape Importer, to easily import a [Gaea terrain](https://quadspinner.com), color maps, weight maps, and masks in Unreal Engine 5 with a single click.

> **Note:** This is a fork updated for **Unreal Engine 5.7** compatibility. For the original plugin, see [QuadSpinner/Gaea2Unreal](https://github.com/QuadSpinner/Gaea2Unreal).

![Unreal Export](screenshots/ue_export.png)
![Unreal Import](screenshots/ue_import.png)

Importing terrains into Unreal Engine can be a complicated process, especially when dealing with various scales. Gaea2Unreal aims to make this an easy, seamless experience for you.

---

## Installation

### Method 1: Download ZIP (Easiest)

1. Click the green **Code** button above → **Download ZIP**
2. Extract the ZIP file
3. Copy the `src` folder contents to your project's `Plugins` folder:
   ```
   YourProject/
   └── Plugins/
       └── GaeaUnrealTools/
           ├── GaeaUnrealTools.uplugin
           ├── Resources/
           └── Source/
   ```
4. Restart Unreal Engine - the plugin will compile automatically

### Method 2: Clone with Git

```bash
cd YourProject/Plugins
git clone https://github.com/DJB443/Gaea2Unreal.git
mv Gaea2Unreal/src GaeaUnrealTools
```

### Method 3: Manual Download

1. Download from [Releases](https://github.com/DJB443/Gaea2Unreal/releases) (if available)
2. Extract to `YourProject/Plugins/GaeaUnrealTools/`

---

## Requirements

- **Unreal Engine 5.7**
- **Visual Studio 2022** with C++ game development workload
- **Windows 10/11 SDK**

---

## Compatibility

| Engine Version | Status |
|----------------|--------|
| UE 5.7 | ✅ Supported |
| UE 5.6 | ❌ Use [original repo](https://github.com/QuadSpinner/Gaea2Unreal) |
| UE 5.5 and below | ❌ Use [original repo](https://github.com/QuadSpinner/Gaea2Unreal) |

---

## Usage

1. Open your UE 5.7 project
2. Click the **Gaea Landscape Importer** button in the toolbar
3. Import your heightmap (`.r16`, `.raw`, or `.png`)
4. The plugin will automatically read the `definition.json` for scale settings
5. Configure weightmaps and materials as needed
6. Click **Create Landscape**

For detailed usage instructions: https://docs.gaea.app/plugins/gaea2unreal/importing-terrains

---

## Troubleshooting

### "Plugin could not be compiled"
- Ensure Visual Studio 2022 is installed with **MSVC v143 toolchain (14.44+)**
- Delete `Binaries` and `Intermediate` folders in the plugin directory
- Regenerate project files (right-click `.uproject` → Generate Visual Studio project files)

### Missing toolbar button
- Go to **Edit → Plugins** and ensure "GaeaUnrealTools" is enabled
- Restart the editor

---

## Changes from Original (UE 5.6 → 5.7)

- Updated `EngineVersion` to 5.7.0
- Replaced deprecated `EditorStyleSet.h` with `Styling/AppStyle.h`
- Updated Landscape API calls:
  - `bCanHaveLayersContent` → `HasLayersContent()`
  - `LandscapeSectionOffset` → `GetSectionBase()`
  - `LayerName` property → `GetLayerName()`/`SetLayerName()`
- Removed deprecated `EditorStyle` module dependency

---

## Credits

- Original plugin by [QuadSpinner](https://quadspinner.com)
- UE 5.7 compatibility update by [DJB443](https://github.com/DJB443)

## Documentation

For usage instructions: https://docs.gaea.app/plugins/gaea2unreal/importing-terrains

## Website

Download Gaea Community Edition for free at https://quadspinner.com
