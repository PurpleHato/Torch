#pragma once

#include "../BaseFactory.h"
#include "../BlobFactory.h"

// DKR assets sit behind a "FixedTable": a u32 count followed by count+1
// offsets relative to the data base (the ROM address right after the
// 16-byte aligned table). Section i spans [offsets[i], offsets[i+1]).
// The table carries no per-entry type, so each section is registered as a
// generic DKR:ASSET (BlobFactory emits raw bytes). Typed factories can be
// wired in later from the decomp's asset index map.
class DKRAssetTableFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, BlobBinaryExporter)
        };
    }
};
