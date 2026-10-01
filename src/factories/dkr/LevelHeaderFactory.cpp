#include "LevelHeaderFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "lib/binarytools/BinaryWriter.h"

#include <algorithm>
#include <cstdint>

// A DKR level-header asset already converted to host order: the 0xC4 fixed header (any
// trailing bytes, e.g. inline AI data, are passed through unchanged).
struct LevelHeaderData : public IParsedData {
    std::vector<uint8_t> bytes;
};

static inline void swp16(uint8_t* b) {
    std::swap(b[0], b[1]);
}
static inline void swp32(uint8_t* b) {
    std::swap(b[0], b[3]);
    std::swap(b[1], b[2]);
}

// Swap the fixed header's multi-byte fields to host order. Single-byte fields, the s8 AI table
// at 0x20 and the wave bytes sharing the 0x70 union stay as they are.
static void byteswap_level_header(uint8_t* base, size_t size) {
    if (size < 0xC4) {
        return;
    }
    swp32(base + 0x08); // course_height (f32)
    static const size_t w16[] = {
        0x34, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x40, 0x42, // geometry, collectables, skybox, fog*
        0x54,                                            // instruments
        0x5A, 0x5E, 0x60, 0x62, 0x64, 0x66,             // waveSineHeight0/1, waveSeedSize, wavePower, unk64/66
        0x68, 0x6E,                                      // waveTexID, waveViewDist
        0x90, 0x92, 0x96, 0x98, 0x9A,                   // weatherEnable/Type/Vel*
        0xA8, 0xAA, 0xB0, 0xBA                          // unkA8/AA/B0/BA
    };
    for (size_t o : w16) {
        swp16(base + o);
    }
    for (size_t o = 0x74; o < 0x90; o += 4) {
        swp32(base + o); // unk74[7], misc asset ids
    }
    swp32(base + 0xA4); // sky texture id
    swp32(base + 0xAC); // pulsing light misc asset id
}

std::optional<std::shared_ptr<IParsedData>> LevelHeaderFactory::parse(std::vector<uint8_t>& buffer,
                                                                         YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    auto data = std::make_shared<LevelHeaderData>();
    data->bytes.assign(segment.data, segment.data + segment.size);
    byteswap_level_header(data->bytes.data(), data->bytes.size());
    return data;
}

ExportResult DKRLevelHeaderBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                  std::string& entryName, YAML::Node& node,
                                                  std::string* replacement) {
    auto data = std::static_pointer_cast<LevelHeaderData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DKRLevelHeader, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*) data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}
