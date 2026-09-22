# s9

A real-time audio visualizer that captures live audio input and displays it across multiple synchronized views: an oscilloscope, a stereo lissajous display, a spectrum analyzer, VU meters, a periodic analyzer with harmonics and waveform views, and a scrolling spectrogram. Built with SDL3 for rendering and PFFFT for spectral analysis.

## Usage

```
s9 [options]
```

| Option | Description | Default |
|---|---|---|
| `-w, --window_width` | Window width, in the display's points | 1280 |
| `-h, --window_height` | Window height, in the display's points | 720 |
| `-a, --audio_device` | Audio input device name | System default |
| `-d, --audio_driver` | Audio driver, by SDL's name for it (`alsa`, `pipewire`, `coreaudio`, ...); the log lists the ones available | SDL's choice |
| `-r, --sample_rate` | Sample rate in Hz | 44100 |
| `-l, --log_level` | Log verbosity (0-3) | 1 |
| `-p, --port` | Port of the parameter server and the web interface | 8080 |
| `--www_dir` | Serve the web interface from a directory instead of the files built into the binary | built-in |
| `--home` | The directory s9 keeps its settings and its cache in | `S9_HOME`, else `./.s9` if it exists, else `~/.s9` |
| `--version` | Print version and git hash, then exit | |

Settings can also be specified in the `settings` file of s9's home directory as key-value pairs (e.g. `sample_rate=48000`), one per line, `#` starting a comment. Command-line arguments override the file.

When the audio input stops delivering or is unplugged, s9 closes it and opens the input again, and while there is none to open it tries again every second.

Keys `1` to `5` select the full, scope, spectrum, periodic, and spectrogram display modes. The mode is also the `s9/mode` parameter below.

The window is drawn at the display's pixel density: on a high-density display it has the same size and layout and more pixels, with the text, lines and traces sharp. The startup log reports the window's size, its size in pixels and the scale it is drawn at.

## Parameters

While running, s9 serves its parameters over [pn](pn/README.md) as a websocket hub on the port given by `--port`: every connected client sends packets and receives everything the server sends, so clients see each other's changes. A client requests the subprotocol `pn` and sends one pn packet per binary message. The same port serves the web interface at `/` and the running program's manifest at `/s9.manifest`.

The schema is declared in [s9_params.c](src/s9_params.c) and its manifest is committed as [s9.manifest](s9.manifest), so a client can read the parameters and their ranges from the file and check the hash the running program reports. `s9_schema`, built alongside `s9`, writes the manifest; `ctest` fails when the committed file no longer matches it. After changing the schema:

```bash
./build/s9_schema > s9.manifest
```

The web interface at `/` is [www/app.js](www/app.js): a control for every parameter, generated from the served manifest, live over the websocket through the JavaScript pn client, which is linked into `www/pn/` from [pn/js](pn/js). It warns when the running program's schema differs from the manifest it was generated from.

[python/s9sd](python/s9sd) is a Stream Deck controller built on this: its dials drive the oscilloscope, its top corner keys step the display mode, it takes its ranges and the mode names from the manifest, and it warns when the server's schema differs. It connects to s9 on this machine at the default port; `--url` points it elsewhere.

## Build

### Dependencies

The build needs a C compiler, CMake, pkg-config, and three libraries: SDL3, SDL3_ttf, and libwebsockets. The FFT is [PFFFT](extern/pffft/pffft.h), compiled in from the tree. pn's protobuf sources are committed, so nothing generates code at build time. The pn submodule must be checked out.

**Linux (Debian/Ubuntu):**
```bash
sudo apt install build-essential cmake pkg-config git libsdl3-dev libsdl3-ttf-dev libwebsockets-dev
```

**macOS:**
```bash
brew install cmake pkg-config sdl3 sdl3_ttf libwebsockets
```

**After cloning:**
```bash
git submodule update --init
```

### Compile

```bash
mkdir build && cd build
cmake ..
make
```

### Test

```bash
ctest --test-dir build
```

The C tests need nothing beyond the build. Two tests use more when it is there and are skipped otherwise: the JavaScript pn client's tests run under Node (`apt install nodejs` or `brew install node`), and the websocket hub's test drives it from pn's Python package, which lives in a venv (below).

### Python tooling

Three parts of the tree are Python, each with its own venv made by its own script; none is needed to build or run s9:

- [pn/python](pn/python), the pn implementation in Python with the `pn-manifest` tool and the hub test: `pn/make_venv.sh`.
- [python](python), the Stream Deck controller: `python/make_venv.sh`.
- [validation](validation), the numeric oracle that generates the golden tables the DSP tests check against: `validation/make_venv.sh`.

They need `python3-venv` (`apt install python3-venv`; Homebrew's Python includes it). A venv is bound to the Python that made it, so after a Python upgrade delete it and run its script again.

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

### Package

The build makes a Debian package for the machine it runs on: the program, this document and the copyright notices, depending on the runtime libraries the program links.

```bash
cmake --build build --target package
sudo apt install ./build/s9_0.0.1_arm64.deb
```

The version is the last tag of the form `vX.Y.Z`. A build past the tag carries the commits since and the hash, as in `0.0.1+3.g9734b76`, which apt orders after the tag and before the next one; uncommitted changes add `+dirty`. CMake reads the version when it configures, which a commit triggers again; after tagging, run `cmake -S . -B build` before packaging.

## License

Copyright Shea Ako, all rights reserved for now; see [LICENSE](LICENSE). The third-party pieces compiled in, and their licenses, are listed in [packaging/copyright](packaging/copyright).