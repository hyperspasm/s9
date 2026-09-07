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
| `-p, --port` | Port of the parameter server and the web interface | 8080 |
| `--www_dir` | Serve the web interface from a directory instead of the files built into the binary | built-in |
| `--version` | Print version and git hash, then exit | |

Settings can also be specified in `~/.s9rc` as key-value pairs (e.g. `sample_rate=48000`). Command-line arguments override the config file.

Keys `1`, `2`, and `3` select the full, scope, and spectrum display modes. The mode is also the `s9/mode` parameter below.

## Parameters

While running, s9 serves its parameters over [pn](pn/README.md) on the Unix socket `/tmp/s9.sock`. The schema is declared in [s9_params.c](src/s9_params.c) and its manifest is committed as [s9.manifest](s9.manifest), so a client can read the parameters and their ranges from the file and check the hash the running program reports. `s9_schema`, built alongside `s9`, writes the manifest; `ctest` fails when the committed file no longer matches it. After changing the schema:

```bash
./build/s9_schema > s9.manifest
```

[python/s9sd](python/s9sd) is a Stream Deck dial controller built on this: it takes its ranges from the manifest and warns when the server's schema differs.

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