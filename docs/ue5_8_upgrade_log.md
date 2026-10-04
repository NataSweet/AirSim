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

### Optional SUV content

`--no-full-poly-car` deliberately skips AirSim's optional high-poly SUV asset
archive. The Blocks `FlyingExampleMap` references materials from that archive,
so opening this map without it produces missing-package errors and fallback or
incorrect vehicle texturing. To download and deploy the SUV assets, rerun the
same command **without** `--no-full-poly-car`:

```powershell
cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" && call build.cmd'
```

The assets are build-time downloaded content, not a source change to commit.
They must exist both under `Unreal/Plugins/AirSim/Content/VehicleAdv/SUV` and
under the deployed Blocks plugin before Unreal indexes the map.

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

## Runtime fixes discovered after the first editor launch

- `AAirSimCameraDirector::EndPlay` must call `Super::EndPlay(EndPlayReason)`.
  Without it, UE 5.8 reports an ensure failure when destroying the camera
  director: `EndPlay on CameraDirector failed`.
- `APIPCamera::EndPlay` must also call `Super::EndPlay(EndPlayReason)`. The
  camera director spawns an `APIPCamera` named `ExternalCamera`; without the
  call UE 5.8 reports `EndPlay on ExternalCamera failed` when ending Play.
- `OpticalFlowMaterial` and `OpticalFlowRGBMaterial` contain Custom HLSL. The
  UE 4.25 code used `View.PrevViewProj`; it was migrated to
  `ResolvedView.RelativeWorldToClip` and `ResolvedView.ClipToPrevClip` for
  UE 5.8. `ResolvedView` is required because Custom material expressions no
  longer expose the old `View` identifier. Without this change, both
  materials fail to compile for `PCD3D_SM5` and UE falls back to the default
  material.
- The first build intentionally used `--no-full-poly-car`; the official
  `car_assets.zip` was subsequently restored and its `VehicleAdv/SUV` folder
  deployed to Blocks. This supplies the materials referenced by
  `FlyingExampleMap` (including glass, metal, leather, reflector, plastic,
  and caliper materials).

## Follow-up runtime checks

Compilation does not validate simulation behavior. Before treating the port as
release-ready, open Blocks in UE 5.8 and verify:

1. `BP_CameraDirector` loads through the Core Redirect.
2. The default multirotor and car spawn successfully.
3. Car steering, throttle, braking, reset, and wheel contact behave correctly
   under Chaos.
4. Camera capture, segmentation, RPC connectivity, and the settings-based
   vehicle modes work in Play In Editor.

## Packaging compatibility follow-up

- `FCameraFocusSettings::bDrawDebugFocusPlane` is editor-only in UE 5.8.
  `APIPCamera::enableFocusPlane` is therefore compiled only when editor-only
  data is available. The focus-plane overlay is a debugging aid and has no
  packaged-game equivalent.
- Added a value constructor to `FDetectionInfo`. This explicitly initializes
  its reflected actor pointer, 2D/3D bounds, and transform, satisfying UE
  reflection validation during cooking.
- Assigned the camera director's SpringArm as its native root component,
  removing UE's arbitrary-root warning for `BP_CameraDirector`.
- Replaced deprecated skeletal-mesh accessors, widget removal, and the two
  MSVC `swscanf` calls with their current UE/MSVC equivalents.
- Updated Blocks' default game mode to the existing
  `/Script/AirSim.AirSimGameMode`, and raised the obsolete `IOS_11` enum value
  to UE 5.8's supported `IOS_15`. These config changes remove load-time
  errors; they do not add iOS to the Windows-only package target.
