import AVFoundation
import os

/// Plays the PSG's samples through AVAudioEngine.
///
/// The emulator runs on the main thread (one frame per display refresh) and pushes about
/// 800 samples per frame; the audio thread pulls them from a small ring buffer. If the
/// buffer gets too full (the display runs a hair faster than a real Master System) the
/// oldest samples are dropped, keeping latency under ~0.1 s.
final class AudioOutput {
    private let engine = AVAudioEngine()
    private let ring = AudioRingBuffer(capacity: 16_384)
    private var configObserver: NSObjectProtocol?
    private(set) var sampleRate: Double = 48_000

    var isMuted = false {
        didSet { engine.mainMixerNode.outputVolume = isMuted ? 0 : 1 }
    }

    init() {
        let session = AVAudioSession.sharedInstance()
        // .playback so games have sound with the silent switch on; mix so music apps keep playing
        try? session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
        try? session.setPreferredIOBufferDuration(0.01)
        try? session.setActive(true)
        if session.sampleRate > 0 { sampleRate = session.sampleRate }

        let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 1)!
        let source = AVAudioSourceNode(format: format, renderBlock: AudioOutput.makeRenderBlock(ring))
        engine.attach(source)
        engine.connect(source, to: engine.mainMixerNode, format: format)

        // Headphones plugged in / route changes stop the engine; start it again
        configObserver = NotificationCenter.default.addObserver(
            forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main
        ) { [weak self] _ in
            MainActor.assumeIsolated { self?.restartIfNeeded() }
        }
    }

    isolated deinit {
        if let configObserver { NotificationCenter.default.removeObserver(configObserver) }
        engine.stop()
    }

    func start() {
        ring.clear()
        restartIfNeeded()
    }

    func stop() {
        engine.pause()
        ring.clear()
    }

    func push(_ samples: [Float]) {
        ring.write(samples, maxBuffered: Int(sampleRate * 0.1))
    }

    private func restartIfNeeded() {
        if !engine.isRunning { try? engine.start() }
    }

    /// Built outside the main actor: it runs on the real-time audio thread.
    nonisolated private static func makeRenderBlock(_ ring: AudioRingBuffer) -> AVAudioSourceNodeRenderBlock {
        { _, _, frameCount, audioBufferList in
            let buffers = UnsafeMutableAudioBufferListPointer(audioBufferList)
            for buffer in buffers {
                guard let data = buffer.mData else { continue }
                ring.read(into: data.assumingMemoryBound(to: Float.self), count: Int(frameCount))
            }
            return noErr
        }
    }
}

/// Single producer (emulator) / single consumer (audio thread) sample buffer.
nonisolated final class AudioRingBuffer: @unchecked Sendable {
    private let capacity: Int
    private let buffer: UnsafeMutablePointer<Float>
    private let lock = OSAllocatedUnfairLock()
    private var readIndex = 0
    private var count = 0
    private var lastSample: Float = 0

    init(capacity: Int) {
        self.capacity = capacity
        buffer = .allocate(capacity: capacity)
        buffer.initialize(repeating: 0, count: capacity)
    }

    deinit { buffer.deallocate() }

    func write(_ samples: [Float], maxBuffered: Int) {
        lock.lock(); defer { lock.unlock() }
        for sample in samples {
            if count == capacity { readIndex = (readIndex + 1) % capacity; count -= 1 }
            buffer[(readIndex + count) % capacity] = sample
            count += 1
        }
        // Keep latency bounded: drop the oldest samples beyond maxBuffered
        if count > maxBuffered {
            let drop = count - maxBuffered
            readIndex = (readIndex + drop) % capacity
            count -= drop
        }
    }

    func read(into out: UnsafeMutablePointer<Float>, count n: Int) {
        lock.lock(); defer { lock.unlock() }
        let available = min(n, count)
        for i in 0..<available {
            out[i] = buffer[readIndex]
            readIndex = (readIndex + 1) % capacity
        }
        count -= available
        if available > 0 { lastSample = out[available - 1] }
        // Underrun (e.g. paused or a slow frame): fade out instead of clicking
        for i in available..<n {
            lastSample *= 0.995
            out[i] = lastSample
        }
    }

    func clear() {
        lock.lock(); defer { lock.unlock() }
        readIndex = 0
        count = 0
    }
}
