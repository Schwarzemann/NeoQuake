# NeoQuake (v0.1.0)

A tiny **Quake 1–like** renderer in C++ and OpenGL, focused on loading and drawing classic **BSP29** maps with their **MIP** textures using the Quake palette.
The maps and the original Quake 1 palette is not included. You have to move them yourself from your original copy of the game.

## Build

You’ll need a C++17 compiler and CMake 3.16+. On Windows you also need the **opengl32** library (provided by the SDK).

```bash
# From repo root
cmake -S . -B build
cmake --build build -j
```

Or for Windows systems you can use MinGW.

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release -j
```

## Usage

```bash
./build/NeoQuake ./maps/e1m1.bsp ./lmp/palette.lmp
```

For Windows you might need to use the command like this:

```bash
./build/NeoQuake.exe ./maps/e1m1.bsp ./lmp/palette.lmp
```

## Screenshots

![NeoQuake View 1](https://github.com/Schwarzemann/NeoQuake/blob/main/doc/view1.PNG)
![NeoQuake View 2](https://github.com/Schwarzemann/NeoQuake/blob/main/doc/view2.PNG)
