# LinuxOnPad

An experimental Linux ARM64 emulator project for iPadOS, inspired by the architecture goals of Henge. This repository is only a starter scaffold: it does **not** boot Linux yet.

## Starter stack
- SwiftUI iPadOS shell
- C++ engine stub exposed through a bridging header
- XcodeGen project specification
- GitHub Actions workflow that builds an **unsigned** iOS IPA artifact

## Build locally
Install Xcode and [XcodeGen](https://github.com/yonaskolb/XcodeGen), then:

```sh
xcodegen generate
xcodebuild -project LinuxOnPad.xcodeproj -scheme LinuxOnPad \
  -sdk iphoneos -configuration Release CODE_SIGNING_ALLOWED=NO build
```

## GitHub Actions
Push to `main` or run the workflow manually. It generates the Xcode project, builds an unsigned `.app`, packages an unsigned `.ipa`, and uploads it as an artifact.

Unsigned IPA files require signing before installation with SideStore/AltStore or another compatible sideloading workflow.

## Planned modules
- ARM64 decoder/interpreter and later JIT
- guest memory and MMU
- virtual devices
- Linux kernel/initramfs boot pipeline
- Metal-backed display renderer

No third-party emulator core is included yet.
