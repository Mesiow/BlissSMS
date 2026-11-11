# BlissSMS for iOS

A Swift/SwiftUI port of BlissSMS. The original C emulator in `../BlissSMS` is untouched.

## Layout

```
BlissSMS-iOS/
├── BlissSMS.xcodeproj        open this in Xcode
├── BlissSMS/                 the iOS app (SwiftUI)
│   ├── Library/              ROM library: import from Files, cartridge cards, ROM info
│   └── Game/                 play screen: video, touch pad, controller/keyboard input
└── BlissCore/                Swift package with the emulator core
    └── Sources/BlissCore/Core/   mirrors BlissSMS/Core/*.c
        ├── System.swift      System.c  (wires everything together, runs a frame)
        ├── Z80.swift         Z80.c
        ├── Bus.swift         Bus.c     (memory map + Sega mapper)
        ├── Cart.swift        Cart.c    (ROM + cart ram / .sav files)
        ├── Io.swift          Io.c      (port routing)
        ├── Vdp.swift         Vdp.c     (video)
        ├── Joypad.swift      Joypad.c
        ├── Psg.swift         Psg.c     (sound, finished: tones, noise, sample output)
        ├── SaveState.swift   save states (new, not in the C code)
        ├── Util.swift        Util.c
        └── Cpm.swift         the CP/M test stub from Z80.c (for zexdoc/prelim)
```

Names follow the C code: `z80Foo(z80, ...)` is `foo(...)` on the `Z80` class, and
`struct` fields are camelCase (`process_interrupt_delay` -> `processInterruptDelay`).
Where C passes a register pointer, Swift passes the value and gets the result back:
`incReg8(z80, &z80->bc.hi)` -> `bc.hi = incReg8(bc.hi)`.

## Status

| C file    | Swift        | Status |
|-----------|--------------|--------|
| Z80.c     | Z80.swift    | Ported, passes zexdoc + prelim |
| Bus.c     | Bus.swift    | Ported |
| Cart.c    | Cart.swift   | Ported (saving fixed, see below) |
| Io.c      | Io.swift     | Ported |
| Vdp.c     | Vdp.swift    | Ported (draws into an RGBA buffer instead of an sfImage) |
| Joypad.c  | Joypad.swift | Ported |
| System.c  | System.swift | Ported (input/rendering moved to the app) |
| Util.c    | Util.swift   | Ported |
| Psg.c     | Psg.swift    | Ported and finished (the C version only latched register writes) |
| —         | SaveState.swift | New: save/load the whole machine |

Differences from the C code, all marked with comments:
- Saving: the C `cartLoad` only enabled saving if a .sav already existed, and `cartDumpSram`
  measured the file size after truncating it (so it wrote 0 bytes). The Swift version always
  writes the full 32 KB cart ram to `<rom>.sav` when a game uses it.
- `memoryBusReadU8` falls off the end in C for unmapped reads; Swift returns $FF.
- VRAM reads in the VDP are masked to $3FFF (the C sprite code can read past the end).
- Joypad release sets the bit instead of toggling it (same result, can't get stuck).
- The BIOS enable bit (port $3E) is ignored when no BIOS image is loaded (Ninja Gaiden
  turns it on and would otherwise run into empty memory).

## Sound and save states

- The PSG generates its 3 tone channels + noise at CPU_CLOCK / 16 and averages them down
  to the device's sample rate; the app plays them through AVAudioEngine (`Game/AudioOutput.swift`).
  Sound plays even with the silent switch on and mixes with other audio; it can be turned
  off from the game menu.
- Save states: game menu → Quick Save / Quick Load, or Save States… for 4 more slots with
  screenshots. Files live in Documents/States/<game>/ (visible in the Files app). A state
  only loads into the game it was made from.

## Testing the Z80

```sh
cd BlissCore
swift test                                   # unit tests
BLISS_PRELIM=/path/prelim.com swift test     # + CP/M prelim test
swift run -c release zexrun /path/zexdoc.com # full zexdoc (~4-7 min)

# Render a ROM headlessly and save the last frame as an image
BLISS_RENDER_ROM=/path/game.sms BLISS_RENDER_OUT=/tmp/frame.ppm swift test --filter RenderTests
```

## Controls

On-screen D-pad, buttons 1 and 2, Pause and Reset. Game controllers (MFi / Xbox / PlayStation)
work too: D-pad or left stick, A = 1, B = 2, Menu = Pause. A hardware keyboard uses the same keys
as the SFML version: arrows, A = 1, S = 2, Space = Pause, Tab = Reset.

## Running the app

Open `BlissSMS.xcodeproj`, pick a simulator or your iPhone (set your team under
Signing & Capabilities), and run. Import `.sms` files with the + button or copy them
into Files › On My iPhone › BlissSMS › ROMs.
