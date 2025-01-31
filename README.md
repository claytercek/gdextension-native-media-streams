# Native Media Streams for Godot

A high-performance GDExtension for Godot 4.3+ that provides native media playback capabilities through platform-specific media frameworks. Built with a focus on performance, memory safety, and zero-copy operations where possible.

## Key Features

- **Zero-Copy Operations**: Minimizes memory overhead by using direct texture updates where supported
- **Native Performance**:
  - Direct hardware acceleration through platform APIs
  - Minimal overhead by avoiding unnecessary abstraction layers
  - Efficient memory management using platform-specific optimizations
- **Platform-Optimized**:
  - Uses native media frameworks instead of generic solutions
  - Takes advantage of platform-specific optimizations and features
  - Automatic codec and format support through system frameworks

## Platform Support

### Currently Implemented

- **macOS**: AVFoundation (AVF)

### In Development

- **Windows**: Windows Media Foundation (WMF)

### Planned

- **iOS**: AVFoundation
- **Linux**: GStreamer
- **Android**: MediaCodec

## Why Use Native Media Streams?

### Performance-First Design

- Zero-copy texture updates where supported
- Direct hardware decoder access
- Minimal abstraction overhead
- Platform-optimized memory management

### Platform Integration

- Native DRM support
- System-level codec updates
- Platform-specific optimizations
- Direct access to hardware features

### Maintenance Benefits

- No external codec dependencies
- Automatic security updates via system frameworks
- Smaller binary footprint
- Platform-maintained codec support

## Building from Source

### Prerequisites

- SCons build system
- Platform-specific requirements:
  - macOS: Xcode Command Line Tools
  - (Future) Windows: Visual Studio with Windows SDK
  - (Future) Linux: GCC and VA-API development libraries

### Build Steps

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/claytercek/gdextension-native-media-streams.git

# Build for your platform
scons platform=<platform> target=template_debug
# or
scons platform=<platform> target=template_release
```

## Contributing

We welcome contributions, particularly in these areas:

- Additional platform implementations
- Performance optimizations
- Memory usage improvements
- Platform-specific feature support
- Documentation and examples

## License

This project is licensed under the MIT License - see the LICENSE file for details.
