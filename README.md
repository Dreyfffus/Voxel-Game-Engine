# C++ Voxel Game Engine

This is an in-progress project for a C++ barebones engine which utilise a Core/App project architecture. There are two included projects - one called _GameCore_, and one called _GameEngine_. [Premake](https://github.com/premake/premake-core) is used to generate project files.

Core builds into a static library and is meant to contain common code intended for use in multiple applications. App contains the main engine architecture and builds into an executable and links the Core static library, as well as provides an include path to Core's code.

The `Scripts/` directory contains build scripts for Windows and Linux, and the `Vendor/` directory contains Premake binaries (currently version `5.0-beta2`).

## Game Engine
The Game Engine is meant to include multiple voxel generation features and shading processes utilized for optimization of a voxel based rendered game. It uses the VulkanAPI for rendering purposes. This will eventually become "Meadows" - an Open World Game with custom generetion and optimized rendering pipelines built for far render distance and RayTracing / Modern rasterization techniques.
## Included
- Assets, shaders and the main engine source code, along with utility and fastMath functions (in `Game Engine/Source` and `Game Core/Source`)
- Simple `.gitignore` to ignore project files and binaries
- Premake binaries for Win/Mac/Linux (`v5.0-beta2`)

## License
- UNLICENSE for this repository (see `UNLICENSE.txt` for more details)
- Premake is licensed under BSD 3-Clause (see included LICENSE.txt file for more details)
