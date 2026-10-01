#pragma once

#include "../BaseFactory.h"

// Typed DKR texture (ASSET_TEXTURES_3D section 2, ASSET_TEXTURES_2D section 4).
// On disk a texture is either a raw big-endian blob or a gzip-wrapped one:
//
//   uncompressed:  [TextureHeader 0x20][pixel/cmd bytes] (x numberOfTextures frames)
//   compressed:    [TextureHeader 0x20][LE u32 uncompressedSize][u8 gzipLevel][raw DEFLATE]
//
// The DEFLATE stream decompresses to the full texture -- frame 0's header
// included -- so after inflate the layout is identical to the uncompressed case.
// This factory decompresses (via Gzip, the DKR-wide default) and then swaps
// each frame's TextureHeader fields to host-little-endian, byte-for-byte mirroring
// the runtime's dkr_texheader_to_host (src/port/DkrAssetBridge.c). The result is
// exactly what the decomp dereferences after the runtime swap, retiring that swap
// for this section (Phase 2 of the typed-pipeline migration).
class DKRTextureBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class DKRTextureFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, DKRTextureBinaryExporter)
        };
    }
};
