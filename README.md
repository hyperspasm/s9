# s9

A real-time audio visualizer that captures live audio input and displays it across multiple synchronized views: an oscilloscope, a stereo lissajous display, a spectrogram, and VU meters. Built with SDL2 for rendering and FFTW for spectral analysis.

## Usage

```
s9 [options]
```

| Option | Description | Default |
|---|---|---|
| `-w, --window_width` | Window width in pixels | 1280 |
| `-h, --window_height` | Window height in pixels | 720 |
| `-a, --audio_device` | Audio input device name | System default |
| `-r, --sample_rate` | Sample rate in Hz | 44100 |
| `-l, --log_level` | Log verbosity (0-3) | 1 |

Settings can also be specified in `~/.s9rc` as key-value pairs (e.g. `sample_rate=48000`). Command-line arguments override the config file.

## Build

### Dependencies

**macOS:**
```bash
brew install sdl2 sdl2_ttf fftw
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libfftw3-dev
```

### Compile

```bash
mkdir build && cd build
cmake ..
make
```

### Xcode (macOS)

```bash
mkdir xcode && cd xcode
cmake -G Xcode ..
open s9.xcodeproj
```

### Run

```bash
./s9
```