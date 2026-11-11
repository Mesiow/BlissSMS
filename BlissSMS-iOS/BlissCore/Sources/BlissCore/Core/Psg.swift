// Swift port of BlissSMS/Core/Psg.c + Psg.h (SN76489 sound chip)
//
// The C version stops after latching register writes (psgUpdate is empty and the noise
// channel is ignored). Everything below the "Not in the C code" marks finishes the chip:
//  - 3 square wave tone channels + 1 noise channel, clocked at CPU_CLOCK / 16
//  - 4-bit volume (attenuation) per channel using the C volume table (2 dB steps)
//  - output resampled to `sampleRate` and collected in `samples` for the app to play
//
// Register writes (psgWritePort):
//   latch byte  1 c c t d d d d   c = channel, t = 1 volume / 0 tone, dddd = low 4 bits
//   data byte   0 - d d d d d d   tone: upper 6 bits of the 10-bit tone, volume/noise: low 4 bits

public final class Psg {
    public var cycles: UInt16 = 0

    public var latchedType: UInt8 = 0
    public var latchedChannel: UInt8 = 0

    public var tones = [UInt16](repeating: 0, count: 4)     // tones for each channel (tones[3] = noise control)
    public var volume = [UInt8](repeating: 0xF, count: 4)   // volume for each channel (0xF = off)
    public var volumeTable = [UInt16](repeating: 0, count: 0x10) // volume lookup for the 4 channels (3 pulse, 1 noise)

    // MARK: Not in the C code: generator state

    /// Per-channel down counters (reloaded from `tones` / the noise rate).
    public var counters = [UInt16](repeating: 0, count: 4)
    /// Square wave output of each channel: +1 or -1. polarity[3] is the noise clock flip-flop.
    public var polarity: [Int32] = [1, 1, 1, 1]
    /// Noise shift register (16-bit on the Master System's PSG).
    public var lfsr: UInt16 = 0x8000

    /// Output rate the app plays at (set it to the audio hardware's rate).
    public var sampleRate: Double = 44_100 {
        didSet { ticksPerSample = Double(CPU_CLOCK) / 16 / sampleRate }
    }
    /// Samples produced since the last `takeSamples()`, mono, -1...1.
    public private(set) var samples: [Float] = []

    private var ticksPerSample = Double(CPU_CLOCK) / 16 / 44_100
    private var tickAccumulator = 0.0
    private var sampleSum: Int32 = 0
    private var sampleTicks: Int32 = 0
    private var dcIn: Float = 0
    private var dcOut: Float = 0

    /// psgInit
    public init() {
        let twoDecibels: Float = 0.8 // each volume in the table up to 0xF is 2 decibels quieter than the previous
        let maxVolume: Int32 = 8000

        var currVolume = Float(maxVolume)
        for i in 0..<0x10 {
            volumeTable[i] = UInt16(currVolume)
            currVolume *= twoDecibels // next vol is lower by 2 decibels
        }
        volumeTable[0xF] = 0 // 0b1111 is off

        samples.reserveCapacity(2048)
    }

    /// psgUpdate: advance the chip by `cycles` CPU cycles.
    public func update(_ cycles: UInt8) {
        self.cycles += UInt16(cycles)
        // The PSG runs at the CPU clock divided by 16
        while self.cycles >= 16 {
            self.cycles -= 16
            tick()
        }
    }

    /// psgWritePort
    public func writePort(_ value: UInt8) {
        // Latch register write
        if (value >> 7) & 0x1 != 0 {
            latchedChannel = (value >> 5) & 0x3
            latchedType = (value >> 4) & 0x1

            // Set channels volume
            if latchedType != 0 {
                let vol = value & 0xF
                volume[Int(latchedChannel)] = vol
            }
            // Tone/noise
            else {
                switch latchedChannel {
                case 0, 1, 2: // pulse tones
                    tones[Int(latchedChannel)] &= ~0xF // clear lower nibble (frequency)
                    tones[Int(latchedChannel)] |= UInt16(value & 0xF)

                default: // noise channel (not handled in C)
                    writeNoise(value & 0x7)
                }
            }
        }
        // Second write to set frequency
        else {
            let data = value & 0x3F // total data is 6 bits
            if latchedType != 0 {
                let vol = data & 0xF
                volume[Int(latchedChannel)] = vol
            } else {
                switch latchedChannel {
                case 0, 1, 2:
                    tones[Int(latchedChannel)] &= 0xF
                    tones[Int(latchedChannel)] |= UInt16(data) << 4 // hhhhhh is upper 6 bit value of the 10 bit value

                default: // noise channel (not handled in C)
                    writeNoise(data & 0x7)
                }
            }
        }
    }

    /// Hands the generated samples to the caller and clears the buffer.
    public func takeSamples() -> [Float] {
        let out = samples
        samples.removeAll(keepingCapacity: true)
        return out
    }

    // MARK: Not in the C code: sound generation

    /// Noise control: bit 2 = white noise (1) / periodic (0), bits 0-1 = shift rate.
    /// Writing it resets the shift register.
    private func writeNoise(_ value: UInt8) {
        tones[3] = UInt16(value)
        lfsr = 0x8000
    }

    private var noisePeriod: UInt16 {
        switch tones[3] & 0x3 {
        case 0: return 0x10
        case 1: return 0x20
        case 2: return 0x40
        default: return tones[2] // follow tone channel 2
        }
    }

    /// One PSG clock (CPU_CLOCK / 16).
    @inline(__always) private func tick() {
        // Tone channels: count down, flip the square wave and reload at 0
        for ch in 0..<3 {
            if counters[ch] > 0 { counters[ch] -= 1 }
            if counters[ch] == 0 {
                counters[ch] = tones[ch]
                polarity[ch] = -polarity[ch]
            }
        }

        // Noise: the shift register moves on every second expiry (rising edge of its flip-flop)
        if counters[3] > 0 { counters[3] -= 1 }
        if counters[3] == 0 {
            counters[3] = noisePeriod
            polarity[3] = -polarity[3]
            if polarity[3] > 0 {
                let white = tones[3] & 0x4 != 0
                // White noise taps bits 0 and 3 (Sega's PSG), periodic noise just rotates
                let feedback = white ? ((lfsr & 0x1) ^ ((lfsr >> 3) & 0x1)) : (lfsr & 0x1)
                lfsr = (lfsr >> 1) | (feedback << 15)
            }
        }

        // Mix
        var sum: Int32 = 0
        for ch in 0..<3 {
            let amp = Int32(volumeTable[Int(volume[ch])])
            // Tone values 0 and 1 give a constant high output (games use this to play samples)
            if tones[ch] <= 1 {
                sum += amp
            } else {
                sum += polarity[ch] * amp
            }
        }
        let noiseAmp = Int32(volumeTable[Int(volume[3])])
        sum += (lfsr & 0x1) != 0 ? noiseAmp : -noiseAmp

        // Average all PSG clocks that fall into one output sample (simple low-pass filter)
        sampleSum += sum
        sampleTicks += 1
        tickAccumulator += 1
        if tickAccumulator >= ticksPerSample {
            tickAccumulator -= ticksPerSample
            let raw = Float(sampleSum) / Float(sampleTicks) / 32768
            sampleSum = 0
            sampleTicks = 0

            // DC blocker so constant outputs (samples, silence) don't click
            let out = raw - dcIn + 0.995 * dcOut
            dcIn = raw
            dcOut = out
            samples.append(out)
        }
    }
}
