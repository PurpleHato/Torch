#pragma once

#include "../BaseFactory.h"

// Typed DKR sprite (ASSET_SPRITES, section 12). The asset is a fixed 0x0C header —
// baseTextureId / numberOfFrames (s16), anchor Vec2s (2x s16), unused_field (s32) —
// followed by a variable-length frameTexOffsets byte tail. Swaps the header fields to
// host-little-endian (the tail is endian-safe), byte-identical to the runtime
// dkr_spriteasset_to_host. Retires that runtime swap (Phase 1 typed-pipeline migration).
class DKRSpriteBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DKRSpriteFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, DKRSpriteBinaryExporter)
        };
    }
};
