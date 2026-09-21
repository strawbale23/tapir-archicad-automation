# Build and test

Updated: 2026-09-19T13:11:16+02:00 (Europe/Berlin).

## Windows / Archicad 29

The local development build uses Visual Studio 2022, the v143 C++ toolset, Windows SDK 10.0.22621, CMake and Python. Obtain the matching Archicad API Development Kit from Graphisoft and follow its SDK terms. The SDK is not included in this repository. The SDK path below points to its **Support** directory.

From the repository root in a terminal:

```powershell
python build_local.py --sdk "C:/DevKits/AC29/Support" --version 29
python build_local.py --build
```

If CMake is not on PATH, add `--cmake "C:/Program Files/CMake/bin/cmake.exe"` to both commands. The resulting add-on is:

`archicad-addon/Build/AC29/RelWithDebInfo/TapirAddOn_AC29_Win.apx`

The build helper normalises Windows PATH variable casing. The source retains older-version guards and macOS build support inherited from upstream; those combinations have not been validated for these changes.

## Offline tests

```powershell
python -m pip install -r archicad-addon/Test/AI/requirements.txt
python -m unittest discover -s archicad-addon/Test/AI -p "test_*.py"
cmake -S archicad-addon/Test/AI -B build/portable-tests
cmake --build build/portable-tests --config Debug
ctest --test-dir build/portable-tests -C Debug --output-on-failure
```

The Python tests inspect contracts and fixture safeguards. The three standalone C++ suites exercise wall placement, transformations and drawing placement arithmetic. They do not require Archicad or prove native model behaviour. GitHub Actions is configured to run these offline checks when the repository is published; no GitHub run is claimed yet.

Generate the static command inventory with:

```powershell
python tools/inventory_native_contracts.py --output docs/ai-source-inventory.json
```

## Installing a development build

Keep a copy of the previously installed Tapir APX. In a test Archicad 29 instance, use Add-On Manager to replace that add-on with this build, then restart Archicad if requested. Avoid loading two Tapir versions simultaneously. Verify `GetAddOnVersion` and `GetCommandContracts` through the native JSON interface. Record the precise binary: experimental binaries may share version `1.5.8-ai.4`, so record the binary hash and runtime capabilities too.

To roll back, restore the previous APX through Add-On Manager and restart as required. This build has not been installed as part of preparing this source package.

## Native fixtures

Open a separately saved disposable PLN. Substitute its exact path and the actual JSON API port:

```powershell
python archicad-addon/Test/AI/native_modelling_fixture.py --port 19723 --project "C:/Tests/Disposable.pln"
```

Without `--run`, this only checks project/version/contracts. Adding `--run` creates remote-coordinate fixtures and attempts to clean up returned, fixture-owned IDs. The fixture never saves the project. Unknown outcomes or unreturned IDs require inspection of the local journal before retrying. `native_annotation_fixture.py` provides a separate annotation exercise; use `--help` for its options.

Native tests, visual inspection, Undo and save/reopen checks remain to be carried out. Do not run these scripts on production projects.

The extended exercise is `archicad-addon/Test/AI/native_extended_fixture.py`, with the same --port, --project and --run contract. It has not yet run inside Archicad. See [ai.4 command notes](NATIVE_AI4_COMMANDS.md).
