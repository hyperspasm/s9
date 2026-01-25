# s9

## Build

### Dependencies

**macOS:**
```bash
brew install sdl2 sdl2_ttf fftw cunit
```

**Linux (Debian/Ubuntu):**
```bash
sudo apt install libsdl2-dev libsdl2-ttf-dev libfftw3-dev libcunit1-dev
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