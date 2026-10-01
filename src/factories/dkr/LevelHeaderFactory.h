#pragma once

#include "../BaseFactory.h"

// Typed DKR level header (ASSET_LEVEL_HEADERS, section 23). Swaps the fixed 0xC4 header's
// multi-byte scalar fields to host-little-endian, byte-identical to the runtime
// dkr_levelheader_to_host. Pointer/offset slots (AILevelTable @0x20, unk70 @0x70,
// unk74[] @0x74, unkA4 @0xA4, pulseLightData @0xAC) are intentionally NOT swapped -- they
// are file-relative offsets rebased to pointers at runtime (game.c) -- matching the runtime
// swap exactly. Retires that runtime swap (Phase 1 typed-pipeline migration).
class DKRLevelHeaderBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelHeaderFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, DKRLevelHeaderBinaryExporter)
        };
    }
};
