# AJReflections Lab

AJReflections Lab is an Unreal Engine 5.8.1 C++ and HLSL project exploring real-time, material-aware ray-traced reflections.

The plugin reads the visible surface's Substrate material data, samples a reflection direction, and traces through Unreal's material hit shaders. It shades geometry hits and samples the Skylight environment for misses, then composites the reflection contribution into the lit frame. Debug views expose the surface inputs, ray results, lighting contributions and temporal history.

## Build requirements

- Windows, DirectX 12 and a GPU with hardware ray-tracing support
- A source build of Unreal Engine 5.8.1
- The AJReflections renderer interfaces added to that Engine build

The Engine changes are **not included in this repository**. This is therefore a source project, not a drop-in plugin for an unmodified Unreal installation. The `EngineAssociation` in `AJReflectionsLab.uproject` refers to my local build and must be changed for another installation.

With the matching Engine build, open `AJReflectionsLab.uproject` and compile the Win64 Editor target. The project enables hardware ray tracing and Substrate in its renderer settings.

## Inspecting the result

Open the Unreal console and use:

| Command | Result |
| --- | --- |
| `r.AJReflections.Enable 1` | Enable the renderer integration |
| `r.AJReflections.DebugMode -1` | Show the normally lit scene |
| `r.AJReflections.PresentationMode 0` | Show the scene without the AJ reflection contribution |
| `r.AJReflections.PresentationMode 1` | Add the current reflection contribution |
| `r.AJReflections.DebugMode 20` | Isolate the reflection radiance |
| `r.AJReflections.DebugMode 35` | Inspect accumulated reflection radiance |

`PresentationMode 2` presents the temporal accumulation path, which remains under development. The full diagnostic list is documented alongside `r.AJReflections.DebugMode` in `AJDebugShader.cpp`.

## Where to start in the source

- `Plugins/AJReflections/Source/AJReflections/Private/AJRendererExtension.cpp` — renderer pass sequence
- `Plugins/AJReflections/Shaders/Private/AJPrimaryReflectionSample.usf` — visible-surface sampling
- `Plugins/AJReflections/Shaders/Private/AJMaterialHitRayGen.usf` — material-aware ray tracing and secondary radiance
- `Plugins/AJReflections/Source/AJReflections/Private/AJReflectionCompose.cpp` — presentation and scene-colour composition

This is an active graphics-engineering project, not a packaged Unreal plugin release.
