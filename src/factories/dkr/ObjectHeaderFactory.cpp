#include "ObjectHeaderFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "lib/binarytools/BinaryWriter.h"

#include <algorithm>
#include <cstdint>

// A DKR object-header asset already converted to host order: the 0x78-byte ObjectHeader
// followed by its trailed modelIds[]/objectParticles[]/vehiclePartIds[] arrays. Offset
// fields remain file-relative (the decomp rebases them in load_object_header).
struct DkrObjectHeaderData : public IParsedData {
    std::vector<uint8_t> bytes;
};

static inline void swp16(uint8_t* b) {
    std::swap(b[0], b[1]);
}
static inline void swp32(uint8_t* b) {
    std::swap(b[0], b[3]);
    std::swap(b[1], b[2]);
}
static inline uint32_t rd32le(const uint8_t* b) {
    return (uint32_t) b[0] | ((uint32_t) b[1] << 8) | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
}

// Swap the header's scalar and offset fields, the s16 block, then the trailing arrays (their
// offsets are read back from the already swapped offset fields). The layout is unchanged.
static void byteswap_object_header(uint8_t* base, size_t size) {
    if (size < 0x78) {
        return;
    }
    // s32/f32 header scalars + the 5 offset fields + pad20/unk24 + shadeAmbient/Diffuse.
    static const size_t w32[] = { 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C };
    for (size_t o : w32) {
        swp32(base + o);
    }
    // s16/u16 block (skipping the u8 run 0x3A-0x3D).
    static const size_t w16[] = { 0x30, 0x32, 0x34, 0x36, 0x38, 0x3E, 0x40, 0x42, 0x44,
                                  0x46, 0x48, 0x4A, 0x4C, 0x4E, 0x50 };
    for (size_t o : w16) {
        swp16(base + o);
    }

    const uint32_t modelIdsOff = rd32le(base + 0x10);   // modelIds
    const uint32_t vehPartOff = rd32le(base + 0x14);    // vehiclePartIds
    const uint32_t partOff = rd32le(base + 0x1C);       // objectParticles
    const uint8_t nModels = base[0x55];                 // numberOfModelIds
    const uint8_t nAttach = base[0x56];                 // attachPointCount
    const uint8_t nParts = base[0x57];                  // particleCount

    for (uint32_t i = 0; i < nModels; i++) {
        const size_t o = modelIdsOff + 4u * i;
        if (o + 4 <= size) {
            swp32(base + o);
        }
    }
    for (uint32_t i = 0; i < nAttach; i++) {
        const size_t o = vehPartOff + 4u * i;
        if (o + 4 <= size) {
            swp32(base + o);
        }
    }
    // objectParticles[] is ObjHeaderParticleEntry { s32 upper, s32 lower } per entry.
    for (uint32_t i = 0; i < nParts; i++) {
        const size_t o = partOff + 8u * i;
        if (o + 8 <= size) {
            swp32(base + o);
            swp32(base + o + 4);
        }
    }
}

std::optional<std::shared_ptr<IParsedData>> ObjectHeaderFactory::parse(std::vector<uint8_t>& buffer,
                                                                          YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    auto data = std::make_shared<DkrObjectHeaderData>();
    data->bytes.assign(segment.data, segment.data + segment.size);
    byteswap_object_header(data->bytes.data(), data->bytes.size());
    return data;
}

ExportResult DKRObjectHeaderBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                   std::string& entryName, YAML::Node& node,
                                                   std::string* replacement) {
    auto data = std::static_pointer_cast<DkrObjectHeaderData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DKRObjectHeader, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*) data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}
