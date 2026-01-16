# Godot CEF - Chromium Embedded Framework for Godot 4

A GDExtension that embeds CEF (Chromium Embedded Framework) into Godot, providing a WebView control that:

- **Works with OBS Game Capture** - Unlike WebView2, CEF renders to a texture that OBS can capture
- **Supports 144fps+** - GPU-accelerated rendering
- **Full input support** - Mouse, keyboard, scrolling

## Requirements

- Windows 10/11 64-bit
- Visual Studio 2022
- CMake 3.21+
- Godot 4.3+

## Setup

### 1. Download CEF

Download the **Minimal Distribution** for Windows 64-bit from:
https://cef-builds.spotifycdn.com/index.html

Extract to `cef/` folder so you have `cef/include/cef_app.h`.

### 2. Build CEF Wrapper Library

```bash
mkdir cef_wrapper_build
cd cef_wrapper_build
cmake -G "Visual Studio 17 2022" -A x64 ../cef
cmake --build . --config Release --target libcef_dll_wrapper
cd ..
```

### 3. Build the GDExtension

```bash
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

The DLL will be output to `addons/godot_cef/bin/windows/godot_cef.dll`.

### 4. Copy CEF Runtime Files

Copy these files from `cef/Release/` and `cef/Resources/` to your **Godot executable folder** (where `Godot.exe` is located):

**From `cef/Release/`:**
- `libcef.dll`
- `chrome_elf.dll`
- `d3dcompiler_47.dll`
- `libEGL.dll`
- `libGLESv2.dll`
- `snapshot_blob.bin`
- `v8_context_snapshot.bin`
- `vk_swiftshader.dll`
- `vk_swiftshader_icd.json`
- `vulkan-1.dll`

**From `cef/Resources/`:**
- `chrome_100_percent.pak`
- `chrome_200_percent.pak`
- `resources.pak`
- `icudtl.dat`
- `locales/` (entire folder)

### 5. Copy Addon to Your Project

Copy the `addons/godot_cef/` folder to your Godot project's `addons/` folder.

## Usage

```gdscript
extends Control

var webview: CefWebviewNode

func _ready():
    webview = CefWebviewNode.new()
    add_child(webview)
    webview.set_anchors_preset(Control.PRESET_FULL_RECT)
    webview.load_url("https://example.com")
```

## API

### CefWebviewNode

Inherits: `Control`

#### Methods

| Method | Description |
|--------|-------------|
| `load_url(url: String)` | Load a URL |
| `load_html(html: String, base_url: String = "")` | Load HTML content |
| `get_url() -> String` | Get current URL |
| `execute_javascript(script: String)` | Execute JavaScript |
| `is_gpu_accelerated() -> bool` | Check if GPU rendering is active |
| `get_status() -> String` | Get current status |

#### Properties

| Property | Type | Description |
|----------|------|-------------|
| `url` | String | Initial URL to load |

## Known Limitations

- **Single-process mode**: Currently runs CEF in single-process mode for simplicity. This is less stable than multi-process but avoids subprocess complexity.
- **Windows only**: macOS/Linux support not implemented yet.

## License

MIT
