# Q4J K-Line Viewer

Native C++/Qt remake of the web K-line workbench.

## Requirements

- CMake 3.21+
- Qt 6 with `Widgets`, `Network`, and `WebSockets`
- C++20 compiler

## Build

```bash
cmake -S . -B build
cmake --build build
./build/q4j_kline_viewer
```

## Package for Windows

On a Windows machine with Qt 6, CMake, and Visual Studio Build Tools installed:

```powershell
cd cpp-kline-viewer
.\package-windows.ps1
```

The script builds `q4j_kline_viewer.exe`, runs `windeployqt`, and creates:

```text
build-windows/q4j-kline-viewer-windows.zip
```

You can also use the GitHub Actions workflow:

```text
.github/workflows/windows-cpp-kline-viewer.yml
```

This workflow is stored inside this `cpp-kline-viewer` directory so the directory can be published as a standalone GitHub repository. Run it manually from GitHub Actions, then download the `q4j-kline-viewer-windows` artifact. The artifact zip contains `q4j_kline_viewer.exe` and the deployed Qt runtime files directly; it does not contain a second nested zip.

## Backend configuration

The app asks for the backend URL at startup. Use:

```text
HTTP Backend: http://127.0.0.1:8080
WebSocket:    ws://127.0.0.1:8080
```

If the WebSocket field is empty, the app derives it from the HTTP URL.

Environment variables are still used as startup defaults:

```bash
export Q4J_BACKEND_URL=http://127.0.0.1:8080
export Q4J_WS_BASE=ws://127.0.0.1:8080
```

## Backend API

If you want to implement your own backend for this viewer, see:

```text
API.md
API.zh-CN.md
```

## Implemented

- Native candlestick rendering with Qt `QPainter`
- HTTP initial candle preload
- WebSocket live candle updates
- Pan and wheel zoom
- Theme switch
- Compact command header
- Strategy settings dialog for high/low intervals
- TradingView-style transparent layer list in the chart
- Price axis and time axis
- Visible layer drawings and labels for Range, N, N-IN, iFVG, Order, and Marker
- Fixed-shape OHLC sketch that follows hovered candle data

## Notes

This is a native rewrite, not a wrapper around the existing web page. The current native overlay drawings are visual layer renderings based on the visible candle range; wiring the full strategy overlay event API can be added next.
