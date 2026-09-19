-- Advanced tests for string.pack, string.unpack, and string.packsize
-- Covering endianness, fixed integer sizes, floats, and padding

print("=== Testing string.pack advanced formats ===")

-- 1. Endianness with 16-bit integers
local be16 = string.pack(">I2", 0x1234)
local le16 = string.pack("<I2", 0x1234)
assert(#be16 == 2 and #le16 == 2)
assert(string.byte(be16, 1) == 0x12 and string.byte(be16, 2) == 0x34)
assert(string.byte(le16, 1) == 0x34 and string.byte(le16, 2) == 0x12)

local val_be = string.unpack(">I2", be16)
local val_le = string.unpack("<I2", le16)
assert(val_be == 0x1234)
assert(val_le == 0x1234)

-- 2. Signed 16-bit integers (h)
local neg_h = string.pack(">h", -1234)
assert(#neg_h == 2)
local val_neg_h = string.unpack(">h", neg_h)
assert(val_neg_h == -1234)

-- 3. 32-bit integers (i4 / I4) and long integers (l / L)
local be32 = string.pack(">i4", -100000)
assert(#be32 == 4)
assert(string.unpack(">i4", be32) == -100000)

local ube32 = string.pack(">I4", 3000000000)
assert(#ube32 == 4)
assert(string.unpack(">I4", ube32) == 3000000000)

local long_sz = string.packsize("l")
local bel = string.pack(">l", -100000)
assert(#bel == long_sz)
assert(string.unpack(">l", bel) == -100000)

-- 4. Single precision float (f)
local packed_f = string.pack("f", 3.125)
assert(#packed_f == 4)
local unpacked_f = string.unpack("f", packed_f)
assert(math.abs(unpacked_f - 3.125) < 1e-5)

-- 5. Padding byte (x)
local with_pad = string.pack("bxI2", 0x41, 0x1122)
assert(#with_pad == 4)
assert(string.byte(with_pad, 1) == 0x41)
assert(string.byte(with_pad, 2) == 0) -- padding
local ub, ui = string.unpack("bxI2", with_pad)
assert(ub == 0x41 and ui == 0x1122)

-- 6. packsize checks
assert(string.packsize("b") == 1)
assert(string.packsize("h") == 2)
assert(string.packsize("i4") == 4)
assert(string.packsize("l") == long_sz)
assert(string.packsize("f") == 4)
assert(string.packsize("d") == 8)
assert(string.packsize("j") == 8)
assert(string.packsize(">hfl") == 2 + 4 + long_sz)
assert(string.packsize("x") == 1)
assert(string.packsize("xx") == 2)

-- 7. Combined heterogeneous packing
local packet = string.pack(">Bhf", 0x7F, -500, 1.5)
assert(#packet == 1 + 2 + 4)
local b, h, f = string.unpack(">Bhf", packet)
assert(b == 0x7F)
assert(h == -500)
assert(math.abs(f - 1.5) < 1e-5)

print("Advanced string.pack tests passed!")
