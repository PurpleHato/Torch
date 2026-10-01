#pragma once

#include "../BaseFactory.h"
#include "../BlobFactory.h"

#include <string>
#include <vector>
#include <cstdint>

// Walks the DKR ROM FixedTable (50 typed sections). For each data section it splits the
// section into its individual assets using the paired "_TABLE" offset table, then
// dispatches every asset to its typed factory (DKR:OBJECT_HEADER, DKR:ASSET, ...) via
// AddAsset. The factory's own emitted resource is the (section,index)->o2r-path manifest
// the runtime uses to resolve a decomp table index to a typed resource path.
//
// assetId encodes (sectionDataIndex << 20) | localIndex, so per-section resolvers decode
// it without collisions across sections (section < 50, index < 2^20).
struct DkrAssetTableEntry {
    uint32_t assetId;
    std::string path;
};

struct AssetTableData : public IParsedData {
    std::vector<DkrAssetTableEntry> entries;
};

class AssetTableBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class AssetTableFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    // This factory is a dispatcher: its parse() enqueues the per-section sub-assets as a
    // side effect and has no exporter of its own for non-Binary modes. Without this, the
    // modding/XML export pipeline would skip parse() (no exporter for the mode) and never
    // discover the sub-assets -- so the sections never get their YAML emitted.
    bool HasModdedDependencies() override { return true; }

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, AssetTableBinaryExporter)
        };
    }
};
