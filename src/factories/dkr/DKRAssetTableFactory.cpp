#include "DKRAssetTableFactory.h"

#include "Companion.h"
#include "types/RawBuffer.h"
#include "spdlog/spdlog.h"

namespace {
inline uint32_t ReadBE32(const std::vector<uint8_t>& buffer, size_t off) {
    return ((uint32_t)buffer[off] << 24) | ((uint32_t)buffer[off + 1] << 16) |
           ((uint32_t)buffer[off + 2] << 8) | (uint32_t)buffer[off + 3];
}
} // namespace

std::optional<std::shared_ptr<IParsedData>> DKRAssetTableFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t tableOffset = GetSafeNode<uint32_t>(node, "offset");

    if ((size_t)tableOffset + 8 > buffer.size()) {
        SPDLOG_ERROR("[DKR] Asset table offset 0x{:08X} out of bounds (rom size 0x{:X}).", tableOffset, buffer.size());
        return std::nullopt;
    }

    const uint32_t count = ReadBE32(buffer, tableOffset);

    // count is followed by count+1 offset words; the table is 16-byte aligned and the
    // section data starts right after it.
    const uint32_t tableSize = ((4u * (count + 2u) + 15u) / 16u) * 16u;
    const uint32_t dataBase = tableOffset + tableSize;

    SPDLOG_INFO("[DKR] Asset table @ 0x{:08X}: {} sections, data base @ 0x{:08X}, table size 0x{:X}",
                tableOffset, count, dataBase, tableSize);

    for (uint32_t i = 0; i < count; i++) {
        const size_t entryOff = (size_t)tableOffset + 4u + 4u * i;
        if (entryOff + 8 > buffer.size()) {
            SPDLOG_WARN("[DKR] Ran past ROM reading asset table entry {}.", i);
            break;
        }

        const uint32_t rel = ReadBE32(buffer, entryOff);
        const uint32_t nextRel = ReadBE32(buffer, entryOff + 4u);
        const uint32_t size = nextRel - rel;

        // Zero-size entries are placeholders that reuse the following section's offset.
        // Torch keys assets by offset, so registering the empty slot would shadow the real one.
        if (size == 0) {
            continue;
        }

        const uint32_t fileOffset = dataBase + rel;

        if ((size_t)fileOffset + size > buffer.size()) {
            SPDLOG_WARN("[DKR] Asset {} (offset 0x{:08X}, size 0x{:X}) past ROM bounds, skipping.",
                        i, fileOffset, size);
            continue;
        }

        YAML::Node assetNode;
        assetNode["type"] = "DKR:ASSET";
        assetNode["offset"] = fileOffset;
        assetNode["size"] = size;
        assetNode["symbol"] = "dkr_asset_" + std::to_string(i);
        Companion::Instance->AddAsset(assetNode);
    }

    // Hand back the raw table bytes so the table node itself has a blob.
    return std::make_shared<RawBuffer>(buffer.data() + tableOffset, tableSize);
}
