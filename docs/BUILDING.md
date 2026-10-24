# Build the native application

Requirements: CMake 3.24+, C++20 compiler, Qt 6.5+ Core/Gui/Widgets/Network/Svg; Qt Test for tests. Release Windows binaries use unmodified Qt 6.8.3 and MinGW GCC 13.1 x64. Python is not a build or runtime requirement for this application.

## Windows

Install Qt 6.8.3 MinGW x64 with SVG/imageformats and the matching MinGW 13.1 compiler. Add Qt/bin and MinGW/bin to PATH. In a developer PowerShell:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
windeployqt --release --no-opengl-sw --no-system-d3d-compiler --dir dist/YT-Chatterino build/YT-Chatterino.exe
Copy-Item build/YT-Chatterino.exe dist/YT-Chatterino/
```

Deploy shared Qt and GCC runtime libraries. Keep plugins in their original folders. The Windows resource embeds the app icon and version. Run tests with the same Qt/compiler DLLs on PATH and `QT_QPA_PLATFORM=offscreen` if there is no desktop.

## Arch Linux

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-svg qt6-imageformats libsecret ttf-dejavu
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel 2
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
./build/yt-chatterino
```

Build the pacman package as a regular user with `cd packaging/arch && makepkg -si`. The PKGBUILD uses this local checkout. Native CI builds and tests on Windows and Arch before attaching an Arch package to a release.

`--profile /absolute/directory` uses an isolated `.ytchatterino.json` and an independent encrypted credential namespace. Test fixtures never require real tokens. Do not check private profiles or OAuth JSON into Git.

Modules: `core` (data/parsing/settings), `network` (HTTP/images), `accounts` (OAuth/vault), `chat` (sources/actions), `view` (virtual chat/panes), `application`/`settings` (windows), `updater` (verified transactions). CMake's `APP_VERSION` is the single application version.
