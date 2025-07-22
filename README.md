
# Mux Sweeper (muxsw)

**Ultra-lightweight, zero-dependency screen capture for Windows. Built in pure C99.**

**MVP Status:** Video-only recording (31KB CLI, 32KB GUI) - Audio features coming in post-MVP

## Key Features

- **Hardware-Accelerated** - DXGI Desktop Duplication API for maximum performance
- **Tiny Binaries** - 31KB CLI, 32KB GUI with aggressive MSVC optimizations  
- **Zero Dependencies** - Pure Windows APIs (DirectX, WMF) - no runtime required
- **Precision Capture** - Full screen, specific monitors, or custom regions
- **Cursor Control** - Toggle cursor visibility in recordings
- **Dual Interface** - Full-featured GUI + powerful CLI
- **Modern Build** - CMake with comprehensive test suite

## Quick Start

**Download latest release** or build from source:

```powershell
# Build (requires VS and CMake)
git clone https://github.com/rmguney/mux-sweeper.git
cd mux-sweeper
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Record your screen:**

```powershell
# GUI
.\release\muxsw-gui.exe

# CLI - 10 second recording
.\release\muxsw.exe --time 10 --out capture.mp4

# Advanced - Monitor 2, region capture, 60fps
.\release\muxsw.exe --monitor 2 --region 100 100 1920 1080 --fps 60 --out demo.mp4
```

## Post-MVP Roadmap

- **Audio Capture** - WASAPI system audio + microphone support
- **Webcam Integration** - Multi-source recording
- **Advanced Controls** - Custom codecs, bitrates, dual-track audio
- **Plugin Architecture** - Modular, opt-in feature system

## Technical Details

- **Language:** Pure C99 (ISO/IEC 9899:1999)
- **APIs:** DXGI, Windows Media Foundation, Win32
- **Compiler:** MSVC with aggressive size optimizations (`/Os`, `/GL`, `/LTCG`)
- **Architecture:** x64 Windows (10+)
- **Output:** H.264/MP4 via hardware-accelerated encoding

## License

MIT License - see [LICENSE](LICENSE) for details.
