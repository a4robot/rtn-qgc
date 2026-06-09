# Custom Boat QGroundControl - Windows 10 Build Guide

This guide provides step-by-step instructions to install the **absolute minimum prerequisites** and build this custom version of QGroundControl (QGC) from source on Windows 10.

## 1. Install Prerequisites

### 1.1. Visual Studio 2022 Build Tools
QGC requires the MSVC (Microsoft Visual C++) compiler. You do not need the full Visual Studio IDE; the Build Tools are sufficient.
1. Download the **Build Tools for Visual Studio 2022** from the [Microsoft Visual Studio downloads page](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022).
2. Run the installer and select **Desktop development with C++**.
3. Ensure the following components are checked on the right side:
   - MSVC v143 - VS 2022 C++ x64/x86 build tools
   - Windows 10 SDK (or Windows 11 SDK)
   - C++ CMake tools for Windows
4. Click **Install**.

### 1.2. Qt 6
QGC is built heavily on the Qt framework.
1. Download the **Qt Online Installer** from the [Qt downloads page](https://www.qt.io/download-qt-installer) (requires a free Qt account).
2. Run the installer and log in.
3. During component selection, choose **Custom Installation**.
4. Expand the latest **Qt 6.6.x** or **Qt 6.8.x** release. (Check QGC's official docs if a specific minor version is required, but Qt 6.6+ is generally recommended).
5. Select the **MSVC 2019 64-bit** (or MSVC 2022 64-bit if available) component.
6. Under "Developer and Designer Tools", ensure **CMake** and **Ninja** are selected.
7. Click **Next** and complete the installation.

### 1.3. Git
1. Download and install [Git for Windows](https://git-scm.com/download/win).
2. Leave all settings at their defaults during installation.

---

## 2. Clone the Repository
Open a terminal (e.g., Command Prompt or PowerShell) and clone this repository.

*Note: You must clone recursively to fetch necessary submodules like MAVLink.*

```cmd
git clone --recursive https://github.com/your-org/qgroundcontrol.git
cd qgroundcontrol
git checkout feat/custom-boat-integration
```
*(Replace the URL with your actual repository URL).*

---

## 3. Build QGroundControl

We will use the **x64 Native Tools Command Prompt for VS 2022** to ensure the compiler paths are correctly loaded.

1. Open the Windows Start Menu, search for **x64 Native Tools Command Prompt for VS 2022**, and open it.
2. Navigate to your cloned QGC directory:
   ```cmd
   cd path\to\your\qgroundcontrol
   ```
3. Set the `CMAKE_PREFIX_PATH` to point to your Qt installation directory. *(Adjust the path depending on your Qt version and installation drive)*:
   ```cmd
   set CMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2019_64
   ```
4. Create a build directory and configure the CMake project:
   ```cmd
   mkdir build
   cd build
   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
   ```
5. Build the application:
   ```cmd
   cmake --build .
   ```
   *(This step will take a while depending on your computer's performance).*

---

## 4. Run the Custom Boat QGC
Once the build is complete, you can launch the custom QGroundControl directly from the build directory.

```cmd
.\staging\Release\QGroundControl.exe
```

*When you connect your custom boat flight controller, you will automatically see the new telemetry dials (RPM, Fuel, Trim, etc.) and lighting controls anchored to the left side of the Fly View.*
