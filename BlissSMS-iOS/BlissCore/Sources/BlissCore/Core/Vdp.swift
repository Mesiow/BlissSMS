// Swift port of BlissSMS/Core/Vdp.c + Vdp.h
//
// SFML is gone: `pixels` (was an sfImage) is a plain RGBA buffer the VDP draws into,
// and `framebuffer` (was an sfTexture) is the finished frame the app puts on screen.
// Each pixel is a UInt32 whose bytes in memory are R, G, B, A.

// Vdp vram memory map
/*
    $4000---------------------------------------------------------------
    Sprite info table : contains x, y and tile number for each sprite
    $3F00---------------------------------------------------------------
    Screen display : 32x28 table of tile numbers / attributes
    $3800---------------------------------------------------------------
    Sprite / tile patterns, 256..447
    $2000---------------------------------------------------------------
    Sprite / tile patterns, 0..255
    $0000---------------------------------------------------------------
*/

public let DISPLAY_WIDTH = 256
public let DISPLAY_HEIGHT = 192

public enum VdpDisplayState: UInt8, Sendable {
    case visible, hBlank, vBlank
}

public enum VdpDisplayMode: UInt8, Sendable {
    case mode4, mode2
}

public final class Vdp {
    public var vram = [UInt8](repeating: 0, count: 0x4000)
    public var cram = [UInt8](repeating: 0, count: 0x20)

    public var state: VdpDisplayState = .visible
    public var mode: VdpDisplayMode = .mode4

    // Internal vdp registers
    public var registers = [UInt8](repeating: 0xFF, count: 0xB)
    public var cycles: UInt16 = 0

    // Vdp Ports
    public var vdpControl: UInt16 = 0
    public var vdpData: UInt16 = 0
    public var vcounter: UInt16 = 0
    public var hcounter: UInt16 = 0

    // Value returned when games read from vcount because scanlines go up to 262,
    // which a byte cannot return, so it's kept in the 0 - 255 range. The VDP goes
    // through 262 scanlines by repeating some vcount values each frame.
    public var vcountPort: UInt8 = 0
    public var lineCounter: UInt8 = 0xFF

    // Flags
    public var statusFlags: UInt8 = 0 // frame interrupt pending, sprite overflow and sprite collision
    public var secondControlWrite: UInt8 = 0
    public var writesToVram: UInt8 = 0 // if == 1: writes to data port go to vram, otherwise to cram
    public var readbuffer: UInt8 = 0

    public var frameIntPending: UInt8 = 0
    public var lineIntPending: UInt8 = 0

    public var spriteOverflow: UInt8 = 0
    public var spriteCollision: UInt8 = 0
    public var yScroll: UInt8 = 0

    public var priorityBuffer = [UInt8](repeating: 0, count: DISPLAY_WIDTH) // priority of sprites/tiles

    /// Back buffer drawn one scanline at a time (sfImage `pixels` in C).
    public var pixels = [UInt32](repeating: 0xFF00_0000, count: DISPLAY_WIDTH * DISPLAY_HEIGHT)
    /// Last finished frame (sfTexture `framebuffer` in C), updated by bufferPixels().
    public internal(set) var framebuffer = [UInt32](repeating: 0xFF00_0000, count: DISPLAY_WIDTH * DISPLAY_HEIGHT)

    public var displayWidth = UInt16(DISPLAY_WIDTH)
    public var displayHeight = UInt16(DISPLAY_HEIGHT)
    public var frameComplete: UInt8 = 0

    public private(set) weak var io: Io?

    /// vdpInit
    public init() {
        registers[0x0] = 0x36
        registers[0x1] = 0x80
        // defaults name table to 0x3800
        registers[0x2] = 0xFF

        // must set all bits in this register, otherwise the
        // vdp will fetch pattern data and name table data incorrectly
        registers[0x3] = 0xFF

        // bits 0 - 2 should be set otherwise vdp will
        // fetch pattern and name table data incorrectly
        registers[0x4] = 0xFF

        // defaults sprite attribute table to 0x3F00
        registers[0x5] = 0xFF
        // unused bits should be set to 1
        registers[0x6] = 0xFB
        registers[0xA] = 0xFF

        state = .visible
        cycles = 0
        mode = .mode4
        lineIntPending = 0
        frameIntPending = 0

        spriteOverflow = 0
        spriteCollision = 0
        yScroll = 0

        vdpControl = 0x0
        vdpData = 0x0
        hcounter = 0
        vcounter = 0
        vcountPort = 0
        lineCounter = 0xFF

        secondControlWrite = 0x0
        readbuffer = 0x0

        displayWidth = UInt16(DISPLAY_WIDTH)
        displayHeight = UInt16(DISPLAY_HEIGHT)
        frameComplete = 0
    }

    /// vdpConnectIo
    public func connectIo(_ io: Io) {
        self.io = io
    }

    /// vdpUpdate
    public func update(_ cycles: UInt8) {
        self.cycles += UInt16(cycles)
        if self.cycles >= UInt16(CYCLES_PER_SCANLINE) {
            if vcounter <= 192 {
                lineCounter &-= 1

                // underflow from 0 to 0xFF sets line irq and reloads line counter
                if lineCounter == 0xFF {
                    lineCounter = registers[0xA]
                    lineIntPending = 1
                }

                // Render at start of new line
                if isDisplayVisible() != 0 {
                    if isDisplayActive() != 0 {
                        render()
                    }
                }
            }

            self.cycles -= UInt16(CYCLES_PER_SCANLINE)
            vcounter += 1
            vcountPort &+= 1

            scanlineUpdate()
        }
    }

    /// vdpScanlineUpdate
    public func scanlineUpdate() {
        // reload line counter between lines 193 - 261
        if vcounter >= 193 && vcounter <= 261 {
            lineCounter = registers[0xA]
        }

        switch vcounter {
        case 193: yScroll = registers[0x9] // y scroll is only updated once the display enters vblank
        // attempt to generate frame interrupt (Vblank period)
        case 196: frameIntPending = 1
        case 218: vcountPort = 213
        case 262:
            frameComplete = 1
            vcounter = 0
            vcountPort = 0
        default: break
        }
    }

    /// vdpRender
    public func render() {
        setMode()
        if mode == .mode4 {
            renderBackground()
            renderSprites()
        }
    }

    /// vdpRenderBackground
    public func renderBackground() {
        let line = Int(vcounter)
        let nametableBaseAddr = Int(getNameTableBaseAddress())

        let maskFirstCol = (registers[0] >> 5) & 0x1

        var row = line
        row /= 8 // a row is 8 pixels large

        // Horizontal scroll
        let xScroll = registers[0x8]
        var xStartCol = Int((xScroll >> 3) & 0x1F)
        let xFineScroll = Int(xScroll & 0x7)

        let disableXscroll = (registers[0] >> 6) & 0x1 // disable xscrolling for rows 0 - 1

        // Vertical scroll
        let yScroll = Int(self.yScroll)
        let yStartRow = (yScroll >> 3) & 0x1F
        let yFineScroll = yScroll & 0x7

        let disableYscroll = (registers[0] >> 7) & 0x1

        // 32 columns per scanline (32 x 28 tiles)
        for column in 0..<32 {
            // draw 8 pixels of the tile in this column
            for x in 0..<8 {
                var xpixelPos = x + (column * 8)

                var useXscroll = false
                if row > 1 || disableXscroll == 0 {
                    useXscroll = true
                }

                if useXscroll {
                    xpixelPos = xStartCol // horizontal start column
                    xpixelPos *= 8 // each col is 8 pixels
                    xpixelPos += (x + xFineScroll) // add current pixel and fine scroll value to the position
                    xpixelPos = xpixelPos % DISPLAY_WIDTH // wrap with display width if position exceeds it
                }

                // Vertical scrolling
                var useYscroll = true
                // y scrolling is disabled for columns 24 - 32 when disable y scroll is set
                if disableYscroll != 0 && column >= 24 {
                    useYscroll = false
                }

                var verticalOffset = row
                if useYscroll {
                    verticalOffset += yStartRow // add starting row value
                    let addRow = line % 8 // add fine scroll value (might move us to next row)
                    if (addRow + yFineScroll) > 7 {
                        verticalOffset += 1
                    }

                    // wrap
                    let numRows = 28
                    verticalOffset %= numRows
                }

                // Tile map data
                var nametableBaseOffset = nametableBaseAddr
                nametableBaseOffset += verticalOffset * (32 * 2) // 32 tiles per row, 2 bytes per tile
                nametableBaseOffset += column * 2 // each tile is two bytes in memory

                // Get 2 byte tile data
                var tileData = UInt16(vram[(nametableBaseOffset + 1) & 0x3FFF]) << 8
                tileData |= UInt16(vram[nametableBaseOffset & 0x3FFF])

                var priority = (tileData >> 12) & 0x1
                let paletteSelect = (tileData >> 11) & 0x1
                let verticalFlip = (tileData >> 10) & 0x1
                let horizontalFlip = (tileData >> 9) & 0x1
                var patternIndex = Int(tileData & 0x1FF) // pattern index for tile to retrieve

                var offset = line // offset will point to which line of pattern to draw

                if useYscroll {
                    offset += yScroll
                }

                offset %= 8 // tile is 8 pixels so wrap if gone past

                if verticalFlip != 0 {
                    // invert offset
                    offset *= -1
                    offset += 7
                }

                // each pattern is 32 bytes in memory
                patternIndex *= 32
                // each pattern line is 4 bytes, offset contains correct pattern line
                patternIndex += 4 * offset

                // get pattern line data
                let d1 = vram[patternIndex & 0x3FFF]
                let d2 = vram[(patternIndex + 1) & 0x3FFF]
                let d3 = vram[(patternIndex + 2) & 0x3FFF]
                let d4 = vram[(patternIndex + 3) & 0x3FFF]

                var colorBit = UInt8(7 - x) // color is read left to right
                if horizontalFlip != 0 {
                    colorBit = UInt8(x) // color read from right to left
                }

                // Get which palette for this pattern line
                var palette = testBit(d4, colorBit) << 3
                palette |= testBit(d3, colorBit) << 2
                palette |= testBit(d2, colorBit) << 1
                palette |= testBit(d1, colorBit)

                // a tile can only have high priority if it isnt palette 0
                if palette == 0 { priority = 0 }
                if paletteSelect != 0 { palette += 16 } // sprite palette is used

                var color: UInt8 = 0
                if maskFirstCol != 0 && xpixelPos < 8 {
                    let overscanBgdropColor = registers[0x7] & 0xF
                    color = cram[Int(overscanBgdropColor) + 16] // color is from sprite palette
                } else {
                    color = cram[Int(palette)]
                }

                let red = color & 0x3
                let green = (color >> 2) & 0x3
                let blue = (color >> 4) & 0x3

                if xpixelPos >= Int(displayWidth) {
                    continue
                }

                // sprite is drawn under tile if priority is set and pal isnt 0
                if priority != 0 && palette != 0 {
                    priorityBuffer[xpixelPos] = 1
                } else {
                    priorityBuffer[xpixelPos] = 0
                }

                pixels[line * DISPLAY_WIDTH + xpixelPos] = getColor(red, green, blue)
            }
            xStartCol += 1
            xStartCol %= 32 // move onto next column and keep within 32 col range
        }
    }

    /// vdpRenderSprites
    public func renderSprites() {
        let line = Int(vcounter)
        let satBaseAddr = Int(getSpriteAttributeTableBaseAddress())

        let shiftSpritesLeft = (registers[0] >> 3) & 0x1
        let isSprite8x16 = (registers[1] >> 1) & 0x1 // 0 = 8x8, 1 = 8x16
        let isSpriteDoubled = registers[1] & 0x1
        let useSecondPattern = (registers[6] >> 2) & 0x1
        var spriteCount = 0
        var spritesDrawn = [UInt8](repeating: 0, count: DISPLAY_WIDTH)

        var spriteSize = 8
        if isSprite8x16 != 0 || isSpriteDoubled != 0 {
            spriteSize = 16
        }

        for sprite in 0..<64 { // max of 64 sprites
            // get y position of sprite
            var y = Int(vram[satBaseAddr + sprite])
            if y == 0xD0 { break } // sprites not drawn in 192 line display mode if y == 0xD0

            if y > 192 { y -= 0x100 } // wrap when top part of sprite is off screen
            y += 1 // y position is always y + 1

            // Is sprite in range of visible display
            if line >= y && line < (y + spriteSize) {
                spriteCount += 1
                if spriteCount > 8 { // only 8 sprites per line allowed, set overflow
                    spriteOverflow = 1
                    break
                }

                let offsetToXCoord = 128 + (sprite * 2)
                var spriteX = Int(vram[satBaseAddr + offsetToXCoord])
                var patternIndex = Int(vram[satBaseAddr + 1 + offsetToXCoord])

                if shiftSpritesLeft != 0 { spriteX -= 8 }
                if useSecondPattern != 0 { patternIndex += 256 }

                if isSprite8x16 != 0 {
                    // bit 0 of pattern index ignored
                    if y < (line + 9) {
                        patternIndex &= ~0x1
                    }
                }

                // each sprite tile takes 32 bytes in memory
                // (4 bytes per line)
                patternIndex *= 32

                // get mem location for current line being drawn
                // of tile, each line is 4 bytes
                patternIndex += (4 * (line - y))

                // get pattern line data (masked: the C version can read past vram here)
                let d1 = vram[patternIndex & 0x3FFF]
                let d2 = vram[(patternIndex + 1) & 0x3FFF]
                let d3 = vram[(patternIndex + 2) & 0x3FFF]
                let d4 = vram[(patternIndex + 3) & 0x3FFF]

                // render 8 pixels for current tile line
                for i in 0..<8 {
                    let xIdx = i + spriteX

                    if xIdx >= DISPLAY_WIDTH { break } // skip if off screen
                    if xIdx < 8 { continue } // skip if column 0
                    if priorityBuffer[xIdx] != 0 { continue } // dont draw if bg has priority over sprite

                    // Get which palette for this pattern line
                    let bit = UInt8(7 - i)
                    var palette = testBit(d4, bit) << 3
                    palette |= testBit(d3, bit) << 2
                    palette |= testBit(d2, bit) << 1
                    palette |= testBit(d1, bit)

                    // transparent
                    if palette == 0 { continue }

                    if spritesDrawn[xIdx] != 0 {
                        spriteCollision = 1
                        continue
                    }

                    spritesDrawn[xIdx] = 1

                    let color = cram[Int(palette) + 16] // sprites use second palette area

                    let red = color & 0x3
                    let green = (color >> 2) & 0x3
                    let blue = (color >> 4) & 0x3

                    pixels[line * DISPLAY_WIDTH + xIdx] = getColor(red, green, blue)
                }
            }
        }
    }

    /// vdpSetMode
    public func setMode() {
        let mode = (registers[0] >> 2) & 0x1
        if mode == 0x1 {
            self.mode = .mode4
        }
    }

    /// vdpBufferPixels: publish the finished frame.
    public func bufferPixels() {
        framebuffer = pixels
    }

    public func isDisplayVisible() -> UInt8 {
        (registers[1] >> 6) & 0x1
    }

    public func isDisplayActive() -> UInt8 {
        vcounter < 192 ? 1 : 0
    }

    /// vdpFrameComplete: returns 1 once per finished frame and clears the flag.
    public func isFrameComplete() -> UInt8 {
        if frameComplete != 0 {
            frameComplete = 0
            return 1
        }
        return 0
    }

    /// vdpWriteControlPort
    public func writeControlPort(_ value: UInt8) {
        // Command word write
        if secondControlWrite != 0 {
            // Updates most significant byte
            secondControlWrite = 0
            vdpControl &= 0xFF
            vdpControl |= (UInt16(value) << 8)

            // Code register
            let codeReg = getCodeRegister()
            switch codeReg {
            case 0:
                // vram read
                let addressReg = getAddressRegister()
                readbuffer = vram[Int(addressReg)]

                incrementAddressRegister()
                writesToVram = 1

            case 1:
                writesToVram = 1

            case 2:
                // vdp register write

                // First byte written holds the data
                // Second byte written holds the register index/number
                let data = UInt8(vdpControl & 0xFF)
                let registerNumber = value & 0xF

                if registerNumber < 0xB {
                    registers[Int(registerNumber)] = data
                }

                writesToVram = 1

            case 3:
                writesToVram = 0

            default:
                print("invalid code register value")
            }
        } else {
            // Updates least significant byte
            secondControlWrite = 1
            vdpControl &= 0xFF00
            vdpControl |= UInt16(value)
        }
    }

    /// vdpWriteDataPort
    public func writeDataPort(_ value: UInt8) {
        secondControlWrite = 0
        if writesToVram != 0 {
            let addressReg = getAddressRegister()
            vram[Int(addressReg)] = value

            incrementAddressRegister()
        } else { // write to cram
            var addressReg = getAddressRegister()
            // cram is only 32 bytes
            addressReg &= 0x1F
            cram[Int(addressReg)] = value

            incrementAddressRegister()
        }

        readbuffer = value
    }

    /// vdpReadControlPort
    public func readControlPort() -> UInt8 {
        secondControlWrite = 0
        statusFlags = 0x1F
        statusFlags |= (frameIntPending << 7)
        statusFlags |= (spriteOverflow << 6)
        statusFlags |= (spriteCollision << 5)

        // reset
        lineIntPending = 0
        frameIntPending = 0
        spriteOverflow = 0
        spriteCollision = 0

        return statusFlags
    }

    /// vdpReadDataPort
    public func readDataPort() -> UInt8 {
        secondControlWrite = 0

        let addressReg = getAddressRegister()
        let buffer = readbuffer

        // vram is buffered on every data port read regardless of the code register
        // and the contents of the buffer before the buffer update is returned
        readbuffer = vram[Int(addressReg)]

        incrementAddressRegister()

        return buffer
    }

    /// vdpIncrementAddressRegister
    public func incrementAddressRegister() {
        // increment address register
        if getAddressRegister() == 0x3FFF { // overflow to 0
            vdpControl &= 0xC000 // keep code register unchanged
        } else {
            vdpControl &+= 1
        }
    }

    public func getCodeRegister() -> UInt8 {
        UInt8((vdpControl >> 14) & 0x3)
    }

    public func getAddressRegister() -> UInt16 {
        vdpControl & 0x3FFF
    }

    public func getNameTableBaseAddress() -> UInt16 {
        var nameTableAddr = registers[0x2]
        // create word using only bits 1 - 3 of register 2
        // (the C version only handles the 192 line display; other heights fall through)
        nameTableAddr &= 0xF
        nameTableAddr = clearBit(nameTableAddr, 0)

        return UInt16(nameTableAddr) << 10
    }

    public func getSpriteAttributeTableBaseAddress() -> UInt16 {
        // bit 0 and 7 ignored
        let spriteTableAddr = registers[0x5]
        return (UInt16(spriteTableAddr) << 7) & 0x3F00
    }

    /// vdpGetColor: returns an RGBA pixel (bytes R, G, B, A in memory).
    @inline(__always) public func getColor(_ red: UInt8, _ green: UInt8, _ blue: UInt8) -> UInt32 {
        let r = UInt32(getColorShade(red))
        let g = UInt32(getColorShade(green))
        let b = UInt32(getColorShade(blue))
        return r | g << 8 | b << 16 | 0xFF00_0000
    }

    @inline(__always) public func getColorShade(_ color: UInt8) -> UInt8 {
        switch color {
        case 0: return 0
        case 1: return 85
        case 2: return 170 // midpoint between 0 and 255
        default: return 255
        }
    }

    /// vdpPendingInterrupts
    public func pendingInterrupts() -> UInt8 {
        // Check for vblank interrupt
        if frameIntPending != 0 && vBlankIrqReady() != 0 { return 1 }
        // Check for line interrupt
        if lineIntPending != 0 && lineInterruptReady() != 0 { return 1 }

        return 0
    }

    public func vBlankIrqReady() -> UInt8 {
        testBit(registers[1], 5)
    }

    public func lineInterruptReady() -> UInt8 {
        testBit(registers[0], 4)
    }
}
