# UE 5.8 / Chaos upgrade log

## Status

`BlocksEditor Win64 Development` successfully builds with Unreal Engine 5.8 on
Windows. The validation completed through Unreal Header Tool, C++ compilation,
and linking.

The authoritative plugin sources are in `Unreal/Plugins/AirSim`. The copy in
`Unreal/Environments/Blocks/Plugins/AirSim` is a build/deployment artifact;
do not edit it directly. Run `build.cmd` or sync the plugin after changing the
source copy.

## Required local toolchain

- Unreal Engine 5.8 (tested at `D:\Unreal\UE_5.8`)
- Visual Studio 2022 with the x64 C++ tools
- Windows SDK 10.0.19041.0 or newer (the tested machine selected 10.0.26100.0)
- CMake and Ninja, both supplied by the tested Visual Studio installation

`UE_ROOT` is optional. The commands below use the engine path explicitly.

Run `build.cmd` from an **x64 Native Tools Command Prompt for VS 2022**. For
example, from PowerShell:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && call build.cmd --no-full-poly-car'
```

Then compile the project:

```powershell
& 'D:\Unreal\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  BlocksEditor Win64 Development `
  '-Project=D:\Repo\AirSim\Unreal\Environments\Blocks\Blocks.uproject' `
  -WaitMutex -NoHotReload
```

## Ported changes and their rationale

### Physics and project configuration

- Replaced PhysX Vehicles with `ChaosVehiclesPlugin` in the plugin and Blocks
  project descriptors.
- Migrated `ACarPawn`, wheel types, movement component, drivetrain setup, and
  reset logic from PhysX Vehicles to Chaos Vehicles.
- Preserved the original vehicle skeletal-mesh bone names. Changing them to a
  fork-specific asset layout would break the original AirSim vehicle assets.
- Updated Blocks target defaults and include-order rules for UE 5.8.

### UE 5 API changes

- Updated asset-registry filters, rendering interfaces, input delegate method
  pointer types, message-dialog invocation, object-property casting, line
  drawing, and actor validity checks.
- Renamed AirSim's `CameraDirector` C++ type to `AAirSimCameraDirector` and
  its header to `AirSimCameraDirector.h`. UE 5.8 now has its own
  `CameraDirector` type and rejects the duplicate reflected name.
- Added a Core Redirect in Blocks `DefaultEngine.ini` so the existing
  `BP_CameraDirector` asset resolves its previous native parent class.
- Added missing explicit AirLib type aliases and a standard-library include
  which older UE precompiled headers happened to provide indirectly.

### C++20 dependencies and build script

- UE 5.8 compiles the plugin as C++20. Eigen 3.3.7 used by upstream AirSim
  relies on `std::result_of`, removed in C++20. `build.cmd` now downloads
  Eigen 3.4.1r, matching the dependency used by the UE 5.8 Cosys port.
- The script writes a `.airsim-eigen-3.4.1r` marker. If an old Eigen tree is
  present, the script replaces it automatically before rebuilding.
- On this VS/CMake installation, the Visual Studio CMake generator created
  temporary MSBuild projects that did not inherit the UCRT paths. `build.cmd`
  therefore uses `Ninja Multi-Config` when Ninja is available, with the
  original Visual Studio generator as a fallback.
- The AirSim solution is built with `/p:UseEnv=true`, allowing MSBuild to use
  the include and library paths initialized by `vcvars64.bat`.
- Updated four MavLink lambda captures to explicitly capture `this`, avoiding
  C++20 deprecation diagnostics treated as errors by UE.

## What was intentionally not imported

The port selectively copied UE5/Chaos compatibility work. It does **not**
bring in unrelated Cosys-AirSim features such as ROS2, GPU lidar, annotation
systems, Matlab bindings, or project-specific assets and coordinate-system
changes.

## Verification recorded

The successful UE command reported `Result: Succeeded` and generated:

`Unreal/Environments/Blocks/Binaries/Win64/UnrealEditor-Blocks.dll`

`git diff --cached --check` also passed after staging the port.

## Non-blocking warnings

- UE reports that `Shell32.lib`, `dinput8.lib`, and `dxguid.lib` are searched
  by filename in the AirSim module. This slows dependency checking but does
  not prevent the build.
- Several existing floating-point literals produce double-to-float warnings.
  They did not prevent the build.

## Follow-up runtime checks

Compilation does not validate simulation behavior. Before treating the port as
release-ready, open Blocks in UE 5.8 and verify:

1. `BP_CameraDirector` loads through the Core Redirect.
2. The default multirotor and car spawn successfully.
3. Car steering, throttle, braking, reset, and wheel contact behave correctly
   under Chaos.
4. Camera capture, segmentation, RPC connectivity, and the settings-based
   vehicle modes work in Play In Editor.
