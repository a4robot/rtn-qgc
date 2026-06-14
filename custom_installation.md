# Compiling QGroundControl (Custom Installation)

This guide summarizes the steps to build QGroundControl on Linux (Ubuntu) using a custom Qt installation.

## 1. System Dependencies

Install the required development libraries and GStreamer components:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    ccache \
    libsecret-1-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    libgstreamer-plugins-good1.0-dev \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-rtsp \
    gstreamer1.0-gl
```

## 2. Qt Installation & Missing Modules

If your Qt installation is missing modules (e.g., `Graphs`, `HttpServer`), use `aqtinstall` to add them.

### Setup aqtinstall
```bash
# Using an existing virtual environment
~/venv/bin/pip install aqtinstall
```

### Install Required Modules
QGC requires several modules not always included in a default "Desktop" installation. Run this to ensure all are present for Qt 6.10.3:

```bash
~/venv/bin/aqt install-qt linux desktop 6.10.3 linux_gcc_64 \
    --outputdir ~/Qt \
    --modules qtgraphs qtlocation qthttpserver qtpositioning qtspeech \
              qtmultimedia qtserialport qtimageformats qtshadertools \
              qtconnectivity qtquick3d qtsensors qtscxml qtwebsockets
```

## 3. Configuration

Use the `qt-cmake` wrapper provided by your Qt installation to ensure CMake finds the correct Qt version and paths.

```bash
~/Qt/6.10.3/gcc_64/bin/qt-cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQGC_STABLE_BUILD=ON \
    -DQGC_BUILD_TESTING=ON
```

## 4. Compilation

Build the project using CMake. If you encounter segmentation faults in `moc` or OOM errors, reduce the number of parallel jobs.

```bash
# Build with all available cores (may require significant RAM)
cmake --build build --config Release --parallel

# Recommended if build fails or system hangs:
cmake --build build --config Release --parallel 4
```

## 5. Running

After a successful build, the executable is located in the build directory:

```bash
./build/Release/QGroundControl
```
