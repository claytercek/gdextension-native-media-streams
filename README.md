# gdextension

GDExtension template that automatically builds into a self-contained addon for the Godot Asset Library.

### Getting started:
1. Clone this repository (or a new repository with this template) with submodules.
    - `git clone --recurse-submodules https://github.com/nathanfranke/gdextension.git`
    - `cd gdextension`
    - Alternatively, `git submodule update --init --recursive`
2. Build a debug binary for the current platform.
    - `scons`
3. Import, edit, and play `project/` using Godot Engine 4+.
    - `godot --path project/`

### Repository structure:
- `project/` - Godot project boilerplate.
  - `addons/native-media-stream/` - Built binaries, to be distributed to other projects.
  - `demo/` - Scenes and scripts for internal testing.
- `src/` - Source code.
- `godot-cpp/` - Submodule needed for GDExtension compilation.

¹ Before distributing as an addon, all binaries for all platforms must be built and copied to the `bin/` directory. This is done automatically by GitHub Actions.

### LSP/Editor IDE Support:
Running `scons cdb` will generate a `compile_commands.json` file in the root directory. This can be used by language servers and IDEs to provide code completion and other features.

Many editors (like VSCode, Zed, etc.) can use this file to provide better C++ support while editing the extension code.

### Distributing your extension on the Godot Asset Library with GitHub Actions:
1. If needed, go to Repository→Actions→Builds→Run workflow
2. Go to Repository→Actions and download the latest artifact.
3. Test the artifact by extracting the addon into a project.
4. Create a new release on GitHub, uploading the artifact as an asset.
5. On the asset, Right Click→Copy Link to get a direct file URL. Don't use the artifacts directly from GitHub Actions, as they expire.
6. When submitting/updating on the Godot Asset Library, Change "Repository host" to `Custom` and "Download URL" to the one you copied.

### Platform support

| Status | Godot Version | Tested Platform |
| ------ | ------------- | --------------- |
| ✅ | 4.3 | Linux x86_64 (debug) |
| ✅ | 4.3 | Linux x86_64 (release) |
| ✅ | 4.3 | Windows x86_64 (debug) |
| ✅ | 4.3 | Windows x86_64 (release) |
| ✅ | 4.3 | Android arm64v8 (debug) |
| ✅ | 4.3 | Android arm64v8 (release) |
| ❌ | | MacOS (debug) |
| ❌ | | MacOS (release) |
| ❌ | | iOS (debug) |
| ❌ | | iOS (release) |
