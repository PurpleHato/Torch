#pragma once

#include "../BaseFactory.h"

// Wraps a raw DKR asset (the original N64 bytes read straight from the ROM) with
// a typed OTR header. No decoding, no parsing, no re-encoding — just the N64
// asset bytes + a 64-byte header carrying the DKR-specific ResourceType.
// AssetTableFactory sets the per-asset type ("dkr_type") when it dispatches.
class AssetBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, AssetBinaryExporter)
        };
    }
};
