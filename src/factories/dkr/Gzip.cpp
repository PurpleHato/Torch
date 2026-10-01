#include "Gzip.h"

#include <zlib.h>
#include <cstring>

namespace Gzip {

std::optional<std::vector<uint8_t>> Decompress(const uint8_t* data, size_t size) {
    // The wrapper needs the 4-byte size word and the level byte before the
    // DEFLATE payload begins.
    if (size < 5) {
        return std::nullopt;
    }

    const uint32_t outSize = (uint32_t) data[0] | ((uint32_t) data[1] << 8) |
                             ((uint32_t) data[2] << 16) | ((uint32_t) data[3] << 24);

    // Sanity-bound the advertised size. DKR textures and models are kilobytes,
    // never gigabytes, so a huge (or zero) value means these bytes are not a DKR
    // gzip wrapper -- leave them alone.
    if (outSize == 0 || outSize > (64u * 1024u * 1024u)) {
        return std::nullopt;
    }

    std::vector<uint8_t> out(outSize);

    z_stream stream;
    std::memset(&stream, 0, sizeof(stream));
    stream.next_in = const_cast<Bytef*>(data + 5);
    stream.avail_in = static_cast<uInt>(size - 5);
    stream.next_out = out.data();
    stream.avail_out = outSize;

    std::optional<std::vector<uint8_t>> result;
    if (inflateInit2(&stream, -15) == Z_OK) {
        const int rc = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        // Accept only a clean, full decode to the advertised size. That exact
        // round-trip is the guard that lets this run as a default over every DKR
        // asset without risk of touching bytes that merely happen to parse.
        if (rc == Z_STREAM_END && stream.total_out == outSize) {
            result = std::move(out);
        }
    }
    return result;
}

} // namespace Gzip
