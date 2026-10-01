#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

// DKR's own gzip wrapper. None of Rare's assets use a standard gzip (0x1f 0x8b)
// stream; instead every compressed asset is laid out as a 5-byte prefix followed
// by a raw DEFLATE payload:
//
//     [u32 uncompressedSize, little-endian][u8 gzipLevel (always 9)][raw DEFLATE]
//
// That is exactly what the decomp's gzip_inflate consumes (see
// src/port/DkrDecompress.c, which decodes the same bytes with zlib at windowBits
// -15). Porting that decode into Torch lets extraction emit already-decompressed
// host bytes, so the runtime never has to inflate.
namespace Gzip {

// Treats `data` as pointing at the first byte of the wrapper (the little-endian
// size word). Returns the decompressed bytes only when the stream inflates
// cleanly to exactly the advertised uncompressed size; returns std::nullopt
// otherwise. The exact-size check is what makes this safe to run as a default
// "decompress-if-gzip" pass over arbitrary DKR asset bytes -- anything that does
// not round-trip to the header's size is left untouched.
std::optional<std::vector<uint8_t>> Decompress(const uint8_t* data, size_t size);

// Convenience for a wrapper that begins `offset` bytes into a vector.
inline std::optional<std::vector<uint8_t>> Decompress(const std::vector<uint8_t>& buf, size_t offset = 0) {
    if (offset >= buf.size()) {
        return std::nullopt;
    }
    return Decompress(buf.data() + offset, buf.size() - offset);
}

} // namespace Gzip
