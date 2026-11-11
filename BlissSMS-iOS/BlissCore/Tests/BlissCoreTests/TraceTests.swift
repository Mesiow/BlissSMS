import Foundation
import Testing
@testable import BlissCore

/// Debug helper: runs a ROM instruction by instruction and prints the last instructions
/// before PC enters a given range.
///   BLISS_TRACE_ROM=/path/game.sms BLISS_TRACE_FROM=0100 BLISS_TRACE_TO=0200 swift test --filter TraceTests
@Suite struct TraceTests {
    @Test(.enabled(if: ProcessInfo.processInfo.environment["BLISS_TRACE_ROM"] != nil))
    func traceUntilRange() throws {
        let env = ProcessInfo.processInfo.environment
        let sms = try System(cartPath: env["BLISS_TRACE_ROM"]!)
        let from = UInt16(env["BLISS_TRACE_FROM"] ?? "0100", radix: 16)!
        let to = UInt16(env["BLISS_TRACE_TO"] ?? "0200", radix: 16)!

        var history: [String] = []
        for step in 0..<20_000_000 {
            if sms.z80.pc >= from && sms.z80.pc < to && step > 0 {
                print(history.suffix(40).joined(separator: "\n"))
                print("-> entered range at step \(step), PC \(String(sms.z80.pc, radix: 16))")
                return
            }
            let pc = sms.z80.pc
            let bytes = (0..<4).map { String(format: "%02X", sms.bus.memoryBusReadU8(pc &+ UInt16($0))) }.joined(separator: " ")
            history.append("\(sms.z80.debugOutput())  [\(bytes)]")
            if history.count > 200 { history.removeFirst(100) }

            let cycles = sms.z80.clock()
            sms.vdp.update(UInt8(truncatingIfNeeded: cycles))
            if sms.vdp.isFrameComplete() != 0 { sms.joy.update() }
            if sms.z80.processInterruptDelay == 0 {
                sms.z80.handleInterrupts(sms.vdp.pendingInterrupts() != 0)
            }
        }
        print("never entered range")
    }
}
