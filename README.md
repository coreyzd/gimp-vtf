# GIMP VTF plug-in

Opens and exports Valve Texture Format (`.vtf`) files in **GIMP 3** on
**Windows and Linux**, using [VTFLib++](https://github.com/coreyzd/VTFLib).

This is a fork of Tom Edwards' [gimp-vtf](https://github.com/Artfunkel/gimp-vtf),
which targeted GIMP 2.8 on Windows.

## Features

* Opens every VTF version from 7.0 to 7.6, including animated textures,
  cube maps and volume textures (each frame/face/slice becomes a layer).
  HDR textures open as 32-bit floating point images.
* Alpha channels open as layer masks, so the colour of transparent pixels
  isn't lost.
* Exports VTF 7.2 to 7.6 in DXT1/3/5, ATI2N (BC5), ATI1N (BC4), BC7 and the
  common uncompressed formats.
* Every top-level layer group is exported to its own VTF: keep the albedo,
  bump map, masks, etc. of a material in one XCF. The first group is written
  to the file name you choose; the others get `_<group name>` appended.
* Layers in a group can be merged, or used as animation frames, cube map
  faces or volume slices.
* Any layer can be used as the alpha channel.
* Mipmaps, clamping, No LOD, bump/SSBump flags and the LOD control resource.
* Export settings are saved inside the XCF, per layer group. Settings saved
  by gimp-vtf 1.x for GIMP 2.8 (`*.xcf.vtf-settings`) are imported.
* Thumbnails in GIMP's file dialogs.

## Installing

GIMP 3 expects each plug-in in its own folder. Copy the `file-vtf` folder
(containing `file-vtf.exe` on Windows, `file-vtf` on Linux) into your
plug-ins folder and restart GIMP:

| OS      | Plug-ins folder                               |
|---------|-----------------------------------------------|
| Windows | `%APPDATA%\GIMP\3.2\plug-ins\`                |
| Linux   | `~/.config/GIMP/3.2/plug-ins/`                |
| Flatpak | `~/.var/app/org.gimp.GIMP/config/GIMP/3.2/plug-ins/` |

(Use `3.0` instead of `3.2` for GIMP 3.0.) On Linux the file must be
executable. The folder is also shown in GIMP under
*Edit → Preferences → Folders → Plug-ins*.

## Building

You need CMake 3.21+, a C11/C++17 compiler, pkg-config and the GIMP 3
development files. VTFLib++ is included as a git submodule and built
automatically, and is linked statically by default so the plug-in is a
single file.

```sh
git clone --recursive https://github.com/coreyzd/gimp-vtf.git
cd gimp-vtf
# or, in an existing clone: git submodule update --init --recursive
```

### Linux

Install the GIMP development package, e.g. `libgimp-3.0-dev` on Debian/Ubuntu
(Debian 13+, Ubuntu 25.04+) or `gimp-devel` on Fedora, then:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cp -r build/file-vtf ~/.config/GIMP/3.2/plug-ins/
```

`cmake --install build` installs system-wide, into
`<prefix>/lib/gimp/3.0/plug-ins/file-vtf`.

The Flatpak version of GIMP can't load plug-ins linked against a system
GIMP; build inside the GNOME SDK, or use the distribution's GIMP package.

### Windows

GIMP for Windows is built with MinGW, so build the plug-in from an
[MSYS2](https://www.msys2.org/) shell (UCRT64 or CLANG64) rather than with
Visual Studio:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,gimp}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Copy `build\file-vtf` to `%APPDATA%\GIMP\3.2\plug-ins\`. With MinGW the C/C++
runtime is linked statically, so no extra DLLs are needed; the GIMP, GLib and
GTK DLLs come from GIMP itself. Use the CLANG64 environment to match the
official GIMP installer most closely.

### Options

| Option | Default | |
|--------|---------|---|
| `VTFLIB_SOURCE_DIR` | `extern/VTFLib` | Use a different VTFLib++ checkout |
| `GIMP_VTF_SHARED_VTFLIB` | `OFF` | Link VTFLib++ as a shared library, installed next to the plug-in |
| `GIMP_VTF_PLUGIN_DIR` | `lib/gimp/3.0/plug-ins` | Where `cmake --install` puts the plug-in |
| `GIMP_VTF_STATIC_RUNTIME` | `ON` | (MinGW) link libgcc/libstdc++ statically |

## Scripting

The plug-in registers `file-vtf-load`, `file-vtf-load-thumb` and
`file-vtf-export`. Run non-interactively, `file-vtf-export` takes
`pixel-format`, `layers-as`, `version`, `mipmaps`, `no-lod`, `clamp`,
`bump-type`, `lod-control-u`/`-v`, `alpha-layer` and `layer-group` (unset
= every top-level group). Run with last values, it uses the settings stored
in the image by the export dialog. See *Help → Procedure Browser* for
details.

## Changes from 1.x

* Ported to the GIMP 3 plug-in API (GObject plug-in, GEGL buffers, GTK 3).
* VTFLib replaced by VTFLib++; added ATI2N, ATI1N, BC7, BGR(A/X) formats and
  VTF 7.6. HDR textures can be opened.
* Builds on Linux; CMake replaces the Visual Studio 2013 project.
* Export works on a copy of the image: your image, its visibility and its undo
  history are left alone.
* Settings are stored in the XCF (as parasites) instead of a
  `.vtf-settings` file next to it, and no longer touch the Windows registry.
* Layer masks are applied when exporting layers as frames/faces/slices, so
  textures opened with alpha export with alpha.
* Files are read and written through GIO, so non-ASCII paths work on Windows.
* Environment maps are checked for 6 layers; volume textures are written
  without mipmaps (VTFLib++ can't generate them).
* Secondary VTFs are named `<file>_<group>.vtf` with the group name
  lower-cased; the main VTF keeps exactly the name you chose.

## License

LGPL 2.1 or later, like the original plug-in and VTFLib++.
