#include "DKRSpriteFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "lib/binarytools/BinaryWriter.h"

#include <algorithm>
#include <cstdint>

// A DKR sprite asset already converted to host order: the 0x0C header followed by the
// variable-length frameTexOffsets byte tail (numberOfFrames + 1 bytes).
struct DkrSpriteData : public IParsedData {
    std::vector<uint8_t> bytes;
};

static inline void swp16(uint8_t* b) {
    std::swap(b[0], b[1]);
}
static inline void swp32(uint8_t* b) {
    std::swap(b[0], b[3]);
    std::swap(b[1], b[2]);
}

// Byte-for-byte mirror of dkr_spriteasset_to_host (src/port/DkrAssetBridge.c): swap the
// 0x0C header (baseTextureId, numberOfFrames, anchor.x, anchor.y as s16; unused_field as
// s32). The frameTexOffsets tail is raw bytes and endian-safe.
static void byteswap_sprite(uint8_t* base, size_t size) {
    if (size < 0x0C) {
        return;
    }
    swp16(base + 0x00);
    swp16(base + 0x02);
    swp16(base + 0x04);
    swp16(base + 0x06);
    swp32(base + 0x08);
}

std::optional<std::shared_ptr<IParsedData>> DKRSpriteFactory::parse(std::vector<uint8_t>& buffer,
                                                                    YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    auto data = std::make_shared<DkrSpriteData>();
    data->bytes.assign(segment.data, segment.data + segment.size);
    byteswap_sprite(data->bytes.data(), data->bytes.size());
    return data;
}

ExportResult DKRSpriteBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node,
                                             std::string* replacement) {
    auto data = std::static_pointer_cast<DkrSpriteData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DKRSprite, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*) data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}
