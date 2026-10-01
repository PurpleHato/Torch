#include "DKRTextureFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "Gzip.h"
#include "lib/binarytools/BinaryWriter.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdint>

// A texture asset already decompressed and converted to host order: the full blob
// (every frame's 0x20 TextureHeader followed by its pixel/cmd bytes), with each
// frame's multi-byte header fields little-endian.
struct DkrTextureData : public IParsedData {
    std::vector<uint8_t> bytes;
};

static inline void swp16(uint8_t* b) {
    std::swap(b[0], b[1]);
}

static inline uint16_t rd16le(const uint8_t* b) {
    return (uint16_t) b[0] | ((uint16_t) b[1] << 8);
}

// Byte-for-byte mirror of dkr_texheader_to_host (src/port/DkrAssetBridge.c): swap
// the six multi-byte TextureHeader fields of a single frame at `base`. Single-byte
// fields (width/height/format/pos/numberOfInstances/unk*) are endian-safe, and
// cmd_offset is overwritten in RAM by material_init, so none of those are touched.
static void swap_texture_frame(uint8_t* base) {
    static const size_t w16[] = { 0x06, 0x08, 0x0A, 0x12, 0x14, 0x16 };
    for (size_t o : w16) {
        swp16(base + o);
    }
}

// Swap every frame's header. Frame 0 sits at offset 0; each frame is `textureSize`
// bytes (host-order once its own header is swapped), exactly as load_texture walks
// them. Bounded by both the frame count and the buffer size so a malformed
// textureSize can never run off the end.
static void byteswap_texture_frames(uint8_t* base, size_t size) {
    if (size < 0x20) {
        return;
    }

    swap_texture_frame(base);
    uint16_t count = rd16le(base + 0x12) >> 8; // numOfTextures, host-order after the swap
    if (count == 0) {
        count = 1;
    }

    size_t offset = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (offset + 0x20 > size) {
            break;
        }
        if (i > 0) {
            swap_texture_frame(base + offset);
        }
        const uint16_t stride = rd16le(base + offset + 0x16); // this frame's textureSize
        if (stride < 0x20) {
            break; // a frame is at least its own 0x20 header
        }
        offset += stride;
    }
}

std::optional<std::shared_ptr<IParsedData>> DKRTextureFactory::parse(std::vector<uint8_t>& buffer,
                                                                     YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    const uint8_t* raw = segment.data;
    const size_t rawSize = segment.size;

    auto data = std::make_shared<DkrTextureData>();

    if (rawSize >= 0x20 && raw[0x1D] != 0) {
        // Compressed: the gzip wrapper sits right after the on-disk TextureHeader,
        // and its DEFLATE stream decompresses to the whole texture (frame 0's
        // header included). On failure, emit nothing -- the runtime resolver treats
        // a zero-length resource as "absent" and falls back to the legacy path.
        auto inflated = Gzip::Decompress(raw + 0x20, rawSize - 0x20);
        if (!inflated) {
            SPDLOG_WARN("[DKR] texture gzip decode failed (size {}); leaving to legacy path", rawSize);
            return data; // empty bytes
        }
        data->bytes = std::move(*inflated);
    } else {
        // Uncompressed: the raw asset is already the full texture.
        data->bytes.assign(raw, raw + rawSize);
    }

    byteswap_texture_frames(data->bytes.data(), data->bytes.size());
    return data;
}

ExportResult DKRTextureBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                              std::string& entryName, YAML::Node& node,
                                              std::string* replacement) {
    auto data = std::static_pointer_cast<DkrTextureData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DKRTexture, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*) data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}
