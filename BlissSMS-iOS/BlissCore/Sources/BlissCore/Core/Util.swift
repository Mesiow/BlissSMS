// Swift port of BlissSMS/Core/Util.c

/// Returns number of set bits in value
@inline(__always) public func popcount(_ value: UInt8) -> UInt8 {
    UInt8(value.nonzeroBitCount)
}

@inline(__always) public func setBit(_ val: UInt8, _ bit: UInt8) -> UInt8 {
    val | (1 << bit)
}

@inline(__always) public func clearBit(_ val: UInt8, _ bit: UInt8) -> UInt8 {
    val & ~(1 << bit)
}

@inline(__always) public func testBit(_ val: UInt8, _ bit: UInt8) -> UInt8 {
    (val >> bit) & 0x1
}
