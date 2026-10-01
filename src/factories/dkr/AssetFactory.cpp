#include "AssetFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "lib/binarytools/BinaryWriter.h"

// Raw N64 asset bytes + the DKR ResourceType tag chosen by the section.
struct DkrAssetData : public IParsedData {
    Torch::ResourceType type = Torch::ResourceType::Blob;
    std::vector<uint8_t> bytes;
};

std::optional<std::shared_ptr<IParsedData>> AssetFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    // The raw N64 asset chunk.
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);

    auto data = std::make_shared<DkrAssetData>();
    data->type = node["dkr_type"] ? static_cast<Torch::ResourceType>(node["dkr_type"].as<uint32_t>())
                                  : Torch::ResourceType::Blob;

    // Passthrough: ship the raw N64 bytes verbatim (gzip-wrapped or not). The runtime
    // reconstructs the assets.bin slice byte-for-byte; gzip-wrapped sections (level/object
    // models, level object maps, animations) stay wrapped so the decomp's existing
    // gzip_inflate keeps working unchanged (Phase 2b no-glob cutover).
    data->bytes.assign(segment.data, segment.data + segment.size);
    return data;
}

ExportResult AssetBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<DkrAssetData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, data->type, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*)data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}
