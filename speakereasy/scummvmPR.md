# SpeakerEasy: Real PC Speaker Sound for ScummVM

## A Personal Story: From 8086 to SpeakerEasy

Some of my best childhood Christmas memories are tied to LucasArts games. I remember unwrapping Indiana Jones and the Last Crusade, The Secret of Monkey Island, and Loom. Back then, I played them on an 8086 with floppy disks, a CGA monitor, and a PC Speaker. No Sound Blaster, no AdLib — just that iconic beeper producing surprisingly expressive music and sound effects.

Those hours spent trying to whip down a plug using that goddamn hook or negotiating with Stan probably explain a lot about the adult I’ve become: wearing glasses, firmly believing in the quantum nature of three-headed monkeys, and thinking that listening to Aphex Twin or Dubmood is a perfectly reasonable lifestyle choice.

About a year ago, while working with Arduino on a side project, I stumbled upon pdxiv's brilliant repository : https://github.com/pdxiv/monkey-island-pc-speaker-theme-on-arduino . Getting my hands dirty with that code and hearing those tiny, raw tones again was a revelation.

I was so moved that I decided to turn this nostalgia into a Christmas present for my brother. Using ScummVM (which I’ve loved and used for years) to capture the sounds, I realized that a quick Arduino hack could be transformed into a generic External PC Speaker device.

**SpeakerEasy** intends to be that *madeleine de Proust*. It’s a project to recreate that authentic beeper experience : hearing the Indiana Jones theme through a "really lame" speaker as a way of traveling back to Christmas morning, sitting in front of that old PC, completely absorbed in adventure.

## What is SpeakerEasy?

**SpeakerEasy** is a simple protocol for routing PC Speaker sound to real hardware. The current implementation supports an ESP32-based external device, but the protocol is designed to be extensible to other backends (native Windows Beep API, Linux console speaker, etc.).

### The Protocol

SpeakerEasy uses a minimal 5-byte packet format:

```
┌──────────────────┬─────────────────┬─────────────────┐
│ CMD (1 byte)     │ Frequency (2B)  │ Duration (2B)   │
│ 0x06 = NOTE      │ Little-endian   │ Little-endian   │
└──────────────────┴─────────────────┴─────────────────┘
```

- **Frequency**: Hz value (0 = silence)
- **Duration**: Optional, in milliseconds (0 = until next note)

### Melody File Format (.spez)

SpeakerEasy defines a simple binary format for storing PC Speaker melodies:

```
┌─────────────────────────────────────────────────────────┐
│ Note 0: Freq (2B) + Duration (2B)                       │
├─────────────────────────────────────────────────────────┤
│ Note 1: Freq (2B) + Duration (2B)                       │
├─────────────────────────────────────────────────────────┤
│ ...                                                     │
└─────────────────────────────────────────────────────────┘
```

Each note is 4 bytes (little-endian):
- **Frequency**: uint16, Hz (0 = silence/rest)
- **Duration**: uint16, milliseconds

This format can be used to record game audio or create standalone PC Speaker music files.

### Implementations

The protocol can be implemented by any device or program that reads from a serial port, named pipe, or Bluetooth SPP connection.

#### ESP32 External Hardware (Current)

An ESP32-based device receives SpeakerEasy packets via Serial or Bluetooth SPP and drives a piezo speaker.

- **Connection**: USB Serial or Bluetooth SPP
- **Baud rate**: 115200, 8N1
- **Bluetooth UUID**: `00001101-0000-1000-8000-00805F9B34FB`
- **Firmware**: [TODO: Add repository link]

#### Named Pipe Backend (Project)

The ScummVM implementation treats named pipes just like serial ports. This means it should be possible to create a bridge program that reads from a named pipe and drives a real PC Speaker - without any changes to ScummVM itself.

A proof-of-concept Python implementation is provided in `dists/speakereasy/speakereasy_beep.py`. It creates a named pipe and attempts to play tones using the system's native beep capability.

| Platform | Pipe Path | Beep Method |
|----------|-----------|-------------|
| Windows | `\\.\pipe\speakereasy` | `winsound.Beep()` |
| Linux | `/tmp/speakereasy` | `beep` command or `ioctl(KIOCSOUND)` |

**⚠️ Why I couldn't test this properly:** I only have access to a Windows PC with a motherboard speaker, but no admin rights. The Windows `Beep()` API has significant overhead (~10-30ms per call), making it unsuitable for real-time streaming of short notes (16-32ms). The result sounds muffled and choppy. Direct port I/O (via InpOut32) would bypass this limitation, but requires installing a signed kernel driver - which I cannot do without admin access.

On Linux, direct access to the PC Speaker via `ioctl(KIOCSOUND)` on `/dev/console` should work much better, but requires root privileges. If you have a Linux machine with a PC Speaker and root access, you could potentially make this work.

**This is why external hardware like the ESP32 remains the recommended solution** - it provides true real-time playback without OS-level limitations or privilege requirements, and delivers the authentic PC Speaker experience.

## What Was Done

### Core Implementation

- **New driver**: `audio/softsynth/speakereasy.h/.cpp` - Cross-platform serial communication driver
- **Protocol**: Simple 5-byte packet format: `CMD_STREAM_NOTE (0x06) + Freq(2 bytes) + Duration(2 bytes)`
- **Integration**: Hooks into existing `Player_V2` PC Speaker emulation in SCUMM engine

### Platform Support

| Platform | Connection Method | Status |
|----------|------------------|--------|
| Windows  | Serial (COM port) or Bluetooth COM | Tested |
| Linux/macOS | Serial (`/dev/ttyUSB*`, `/dev/tty.*`) | Implemented |
| Android  | Bluetooth SPP via JNI | Tested |

### GUI Integration

- New "Hardware" tab in Global Options
- Checkbox to enable/disable SpeakerEasy
- Text field for port/device name configuration
- Layout support for all themes (highres/lowres)

### Files Modified

```
audio/softsynth/speakereasy.h      # Main driver (Windows/POSIX inline, Android declarations)
audio/softsynth/speakereasy.cpp    # Android JNI implementation
audio/softsynth/pcspk.h/.cpp       # Integration point
audio/module.mk                    # Build configuration

engines/scumm/players/player_v2.h  # SpeakerEasy instance + deduplication
engines/scumm/players/player_v2.cpp # Send notes to external hardware

gui/options.h/.cpp                 # GUI controls
gui/themes/common/*_layout.stx     # Theme layouts

# Android-specific
backends/platform/android/jni-android.h/.cpp
backends/platform/android/org/scummvm/scummvm/BluetoothSerial.java
backends/platform/android/org/scummvm/scummvm/ScummVMActivity.java
dists/android/AndroidManifest.xml
```

### Optimizations

- **Frequency deduplication**: Only sends notes when frequency changes (avoids Bluetooth lag)
- **Efficient protocol**: Minimal 5-byte packets for low latency

## Testing Performed

- **Windows**: Tested with Bluetooth COM port (COM9) on Indiana Jones and the Last Crusade
- **Android**: Tested on Huawei Kirin 970 device with Bluetooth SPP
- **Games tested**: Indiana Jones 3, Maniac Mansion intro

## Configuration

### Windows (Serial/Bluetooth COM)
```
speakereasy_enable=true
speakereasy_port=COM9
```

### Windows (Named Pipe)
```
speakereasy_enable=true
speakereasy_port=\\.\pipe\speakereasy
```

### Linux/macOS (Serial)
```
speakereasy_enable=true
speakereasy_port=/dev/ttyUSB0
```

### Linux/macOS (Named Pipe)
```
speakereasy_enable=true
speakereasy_port=/tmp/speakereasy
```

### Android
```
speakereasy_enable=true
speakereasy_port=Speaker Easy
```
(Use the Bluetooth device name as shown in Android settings)

---

## TODO Before Merge

### Documentation
- [ ] Add SpeakerEasy section to ScummVM documentation/wiki
- [ ] Document the serial protocol for hardware builders
- [ ] Add link to ESP32 firmware repository

### Code Quality
- [ ] Add proper Doxygen comments to public API
- [ ] Review coding style compliance with ScummVM guidelines
- [ ] Consider adding `SPEAKEREASY` feature flag for conditional compilation

### Testing
- [ ] Test on macOS with serial adapter
- [ ] Test on Linux with USB-to-serial adapter
- [ ] Test with more SCUMM games (Loom, Zak McKracken, Maniac Mansion)
- [ ] Verify no performance impact when disabled

### Potential Enhancements
- [ ] Auto-detection of SpeakerEasy device
- [ ] Support for other engines with PC Speaker (AGI, etc.)
- [ ] iOS support (would require External Accessory framework or BLE)

### Build System
- [ ] Ensure builds correctly on all CI platforms
- [ ] Consider making Bluetooth support optional on Android

---

## Related Work

- ScummVM PC Speaker emulation: `audio/softsynth/pcspk.cpp`
- SCUMM V2 player: `engines/scumm/players/player_v2.cpp`
