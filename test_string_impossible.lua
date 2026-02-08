
-- Test "impossible" string extension functions

local function assert_eq(a, b, msg)
    if a ~= b then
        error(string.format("Assertion failed: %s ~= %s (%s)", tostring(a), tostring(b), msg or ""))
    end
end

local function assert_not_nil(a, msg)
    if a == nil then
        error(string.format("Assertion failed: value is nil (%s)", msg or ""))
    end
end

print("Testing impossible string extensions...")

-- AES
-- AES-128 key (16 bytes), IV (16 bytes)
local key = string.rep("k", 16)
local iv = string.rep("i", 16)
local data = "Hello, AES World!"

local encrypted = string.aes_encrypt(key, data, iv)
assert_not_nil(encrypted, "aes_encrypt result")
assert_eq(#encrypted % 16, 0, "encrypted length multiple of 16")

local decrypted = string.aes_decrypt(key, encrypted, iv)
-- Decrypted result will be padded with zeros (buffer size) if we implemented it simply
-- Or if we handle padding correctly, it should match.
-- In current implementation, we alloc padded_len, and decrypt writes data_len.
-- But wait, decrypt implementation takes encrypted length as data_len.
-- So the result of decrypt will be padded size.
-- The original data "Hello, AES World!" is 17 bytes. Padded to 32.
-- So we expect decrypted to be 32 bytes, starting with original data.
assert_eq(string.sub(decrypted, 1, #data), data, "decrypted content match")

-- CRC32
-- CRC32 of "123456789" is 0xCBF43926 (3421780262)
local crc = string.crc32("123456789")
assert_eq(crc, 3421780262, "crc32 check")

-- SHA256
-- SHA256 of "abc"
-- ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
local hash = string.sha256("abc")
assert_eq(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 check")

-- Image Resize
-- Create a small 2x2 red PNG
local red_png_hex =
    "89504e470d0a1a0a0000000d4948445200000002000000020802000000fd" ..
    "d49a73000000097048597300000ec300000ec301c76fa8640000000f4944" ..
    "4154789c63f8cfc00000030601015112773b0000000049454e44ae426082"
-- Wait, the hex string above might not be a valid 2x2 red png.
-- Let's use string.data2png to create one.
-- 2x2 pixels: Red, Green, Blue, White
-- RGB format: 3 bytes per pixel.
-- R: FF0000, G: 00FF00, B: 0000FF, W: FFFFFF
local pixels = string.fromhex("FF0000" .. "00FF00" .. "0000FF" .. "FFFFFF")
-- Width 2
local png_data = string.data2png(pixels, 2)
assert_not_nil(png_data, "data2png result")

-- Resize to 4x4
local resized_png = string.imageresize(png_data, 4, 4)
assert_not_nil(resized_png, "imageresize result")
assert_eq(string.sub(resized_png, 1, 4), "\137\080\078\071", "PNG signature check")

-- Check if we can convert back to data and check size
local resized_pixels = string.png2data(resized_png)
-- 4x4 image, png2data returns 1 byte per pixel (encoded data style) = 16 bytes
assert_eq(#resized_pixels, 4*4, "resized raw data size")

print("All impossible string extension tests passed!")
