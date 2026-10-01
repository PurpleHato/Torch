#include "AssetTableFactory.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "types/RawBuffer.h"
#include "lib/binarytools/BinaryWriter.h"
#include "spdlog/spdlog.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

inline uint32_t RD32(const std::vector<uint8_t>& b, size_t o) {
    return ((uint32_t)b[o] << 24) | ((uint32_t)b[o + 1] << 16) | ((uint32_t)b[o + 2] << 8) | (uint32_t)b[o + 3];
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

// DKR table types (mirrors dkr_assets_tool extract/assetTable.cpp).
enum class DkrTableType { None, Variable, GameText, MenuText, TTGhost, Misc, Audio };

// Parses a _TABLE section into a list of data-section-relative offsets.
// Each table type has a different layout (see assetTable.cpp _parse_*).
std::vector<uint32_t> ParseOffsets(const std::vector<uint8_t>& buf, uint32_t tStart, DkrTableType tt, size_t maxBytes) {
    const size_t end = std::min(buf.size(), (size_t)tStart + maxBytes);
    std::vector<uint32_t> offs;
    switch (tt) {
        case DkrTableType::GameText:
            for (size_t o = tStart; o + 4 <= end; o += 4) {
                const int32_t v = (int32_t)RD32(buf, o);
                if (v == -1) break;
                offs.push_back((uint32_t)(v & 0x7FFFFFFF));
            }
            break;
        case DkrTableType::MenuText:
            for (size_t o = tStart + 4; o + 4 <= end; o += 4) {
                const int32_t v = (int32_t)RD32(buf, o);
                if (v < 0) break;
                offs.push_back((uint32_t)v);
            }
            break;
        case DkrTableType::TTGhost:
            for (size_t o = tStart; o + 12 <= end; o += 8) {
                const int32_t v = (int32_t)RD32(buf, o + 4);
                if (v < 0) break;
                offs.push_back((uint32_t)v);
            }
            break;
        case DkrTableType::Misc:
            for (size_t o = tStart; o + 4 <= end; o += 4) {
                const int32_t v = (int32_t)RD32(buf, o);
                if (v < 0) break;
                offs.push_back((uint32_t)v * 4u);
            }
            break;
        case DkrTableType::Audio:
            for (size_t o = tStart; o + 4 <= end; o += 4) {
                const int32_t v = (int32_t)RD32(buf, o);
                if (v < 0) break;
                offs.push_back((uint32_t)v);
            }
            offs.insert(offs.begin(), 0);
            break;
        case DkrTableType::Variable:
        default:
            for (size_t o = tStart; o + 4 <= end; o += 4) {
                const int32_t v = (int32_t)RD32(buf, o);
                if (v < 0) break;
                offs.push_back((uint32_t)v);
            }
            break;
    }
    return offs;
}

struct DkrSection {
    int dataIndex;
    const char* buildId;
    Torch::ResourceType type;
    const char* folder;
    int tableIndex;       // FixedTable slot of the paired _TABLE (-1 = single)
    DkrTableType tableType;
};

// All 25 data sections (the non-Table, non-Empty, non-deferred entries from
// the 50-slot FixedTable). Table types from assetTable.cpp.
static const DkrSection SECTIONS[] = {
    { 0,  "ASSET_AI_BEHAVIOUR",          Torch::ResourceType::Blob,               "ai-racers",           1,  DkrTableType::Variable },
    { 2,  "ASSET_TEXTURES_3D",           Torch::ResourceType::DKRTexture,         "textures/3d",         3,  DkrTableType::Variable },
    { 4,  "ASSET_TEXTURES_2D",           Torch::ResourceType::DKRTexture,         "textures/2d",         5,  DkrTableType::Variable },
    { 6,  "ASSET_GAME_TEXT",             Torch::ResourceType::Blob,               "text/game",           7,  DkrTableType::GameText },
    { 8,  "ASSET_MENU_TEXT",             Torch::ResourceType::DKRMenuText,        "text/menu",           9,  DkrTableType::MenuText },
    { 10, "ASSET_SCREENS",               Torch::ResourceType::Blob,               "screens",             11, DkrTableType::Variable },
    { 12, "ASSET_SPRITES",               Torch::ResourceType::DKRSprite,          "sprites",             13, DkrTableType::Variable },
    { 15, "ASSET_MISC",                  Torch::ResourceType::DKRMisc,            "misc",                16, DkrTableType::Misc },
    { 17, "ASSET_HUD_ELEMENT_IDS",       Torch::ResourceType::Blob,               "ids",                 -1, DkrTableType::None },
    { 18, "ASSET_MENU_ELEMENT_IDS",      Torch::ResourceType::Blob,               "ids",                 -1, DkrTableType::None },
    { 19, "ASSET_WEATHER_PARTICLES",     Torch::ResourceType::Blob,               "levels",              -1, DkrTableType::None },
    { 21, "ASSET_LEVEL_OBJECT_MAPS",     Torch::ResourceType::DKRLevelObjectMap,  "levels/objectMaps",   20, DkrTableType::Variable },
    { 23, "ASSET_LEVEL_HEADERS",         Torch::ResourceType::DKRLevelHeader,     "levels/headers",      22, DkrTableType::Variable },
    // LEVEL_NAMES (25) is one contiguous names blob the game loads whole (offset 0) and
    // indexes via LEVEL_NAMES_TABLE (24); emit both as single resources, not carved.
    { 24, "ASSET_LEVEL_NAMES_TABLE",     Torch::ResourceType::Blob,               "levels/names",        -1, DkrTableType::None },
    { 25, "ASSET_LEVEL_NAMES",           Torch::ResourceType::Blob,               "levels/names",        -1, DkrTableType::None },
    { 27, "ASSET_LEVEL_MODELS",          Torch::ResourceType::DKRLevelModel,      "levels/models",       26, DkrTableType::Variable },
    { 29, "ASSET_OBJECT_MODELS",         Torch::ResourceType::DKRModel,           "objects/models",      28, DkrTableType::Variable },
    { 30, "ASSET_ANIMATION_IDS",         Torch::ResourceType::Blob,               "ids",                 -1, DkrTableType::None },
    { 32, "ASSET_OBJECT_ANIMATIONS",     Torch::ResourceType::Blob,               "objects/animations",  31, DkrTableType::Variable },
    { 34, "ASSET_OBJECTS",               Torch::ResourceType::DKRObjectHeader,    "objects/headers",     33, DkrTableType::Variable },
    { 35, "ASSET_LEVEL_OBJECT_TRANSLATION_TABLE", Torch::ResourceType::Blob,      "objects",             -1, DkrTableType::None },
    { 39, "ASSET_AUDIO",                 Torch::ResourceType::DKRAudio,           "audio",               38, DkrTableType::Audio },
    { 41, "ASSET_PARTICLES",             Torch::ResourceType::DKRParticle,        "particles/particles", 40, DkrTableType::Variable },
    { 43, "ASSET_PARTICLE_BEHAVIORS",    Torch::ResourceType::DKRParticleBehavior,"particles/behaviors", 42, DkrTableType::Variable },
    { 44, "ASSET_FONTS",                 Torch::ResourceType::Blob,               "fonts",               -1, DkrTableType::None },
    { 45, "ASSET_JAPANESE_FONTS_TABLE",  Torch::ResourceType::Blob,               "fonts",               -1, DkrTableType::None },
    { 46, "ASSET_JAPANESE_FONTS",        Torch::ResourceType::Blob,               "fonts",               -1, DkrTableType::None },
    { 47, "ASSET_DUMMY_PARTICLE_IDS",    Torch::ResourceType::Blob,               "ids",                 -1, DkrTableType::None },
    { 49, "ASSET_TTGHOSTS",              Torch::ResourceType::DKRTTGhost,         "tt_ghosts",           48, DkrTableType::TTGhost },
};

// Maps a section's ResourceType to its typed-factory YAML key. Sections without a
// typed factory yet fall through to "DKR:ASSET" (blob passthrough, which still
// gzip-decompresses by default). Add a case each time a typed factory lands
// (Phase 1: DKRObjectHeader, Phase 2: DKRTexture). See plan lazy-painting-muffin.md.
const char* DkrYamlType(const DkrSection& s) {
    switch (s.type) {
        case Torch::ResourceType::DKRObjectHeader: return "DKR:OBJECT_HEADER";
        case Torch::ResourceType::DKRTexture:      return "DKR:TEXTURE";
        case Torch::ResourceType::DKRSprite:       return "DKR:SPRITE";
        case Torch::ResourceType::DKRLevelHeader:  return "DKR:LEVEL_HEADER";
        case Torch::ResourceType::DKRMenuText:     return "DKR:MENU_TEXT";
        default: return "DKR:ASSET";
    }
}

std::vector<std::string> LoadFriendlyNames(const std::string& buildId) {
    std::vector<std::string> names;
    const std::string metaPath = "dkr_meta/" + Lower(buildId) + ".meta.json";
    try {
        YAML::Node meta = YAML::LoadFile(metaPath);
        YAML::Node files = meta["files"] ? meta["files"] : meta;
        YAML::Node order = files["order"];
        YAML::Node secs = files["sections"];
        if (order) {
            for (const auto& idNode : order) {
                const std::string id = idNode.as<std::string>();
                std::string fn;
                if (secs && secs[id] && secs[id]["filename"]) {
                    fn = secs[id]["filename"].as<std::string>();
                }
                if (fn.size() > 5 && fn.compare(fn.size() - 5, 5, ".json") == 0) fn.resize(fn.size() - 5);
                names.push_back(fn);
            }
        } else if (meta["filename"]) {
            // Single-asset section: one filename at the top level.
            std::string fn = meta["filename"].as<std::string>();
            if (fn.size() > 5 && fn.compare(fn.size() - 5, 5, ".json") == 0) fn.resize(fn.size() - 5);
            names.push_back(fn);
        }
    } catch (...) {
        SPDLOG_WARN("[DKR] Could not load friendly names from {}", metaPath);
    }
    return names;
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> AssetTableFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const uint32_t tableOffset = GetSafeNode<uint32_t>(node, "offset");
    if ((size_t)tableOffset + 8 > buffer.size()) {
        SPDLOG_ERROR("[DKR] Asset table offset 0x{:08X} out of bounds.", tableOffset);
        return std::nullopt;
    }

    const uint32_t count = RD32(buffer, tableOffset);
    const uint32_t tableSize = ((4u * (count + 2u) + 15u) / 16u) * 16u;
    const uint32_t dataBase = tableOffset + tableSize;
    std::vector<uint32_t> ftOff(count + 1);
    for (uint32_t i = 0; i <= count; i++) {
        ftOff[i] = RD32(buffer, tableOffset + 4u + 4u * i);
    }

    Companion::Instance->SetCurrentDirectory("");

    auto tableData = std::make_shared<AssetTableData>();
    size_t totalAssets = 0;

    for (const auto& s : SECTIONS) {
        if (s.dataIndex < 0 || s.dataIndex >= (int)count) continue;
        const uint32_t dStart = dataBase + ftOff[s.dataIndex];
        const uint32_t dSize = ftOff[s.dataIndex + 1] - ftOff[s.dataIndex];
        const auto friendly = LoadFriendlyNames(s.buildId);

        auto emitAsset = [&](uint32_t romOff, uint32_t size, size_t index) {
            if ((size_t)romOff + size > buffer.size()) return;
            if (size == 0) {
                // Zero-size entries share the same ROM offset as the next entry;
                // AddAsset dedupes by address. Use a unique synthetic offset in
                // the ROM padding area so each empty slot gets its own resource.
                // The payload is 0 bytes so this offset is never dereferenced.
                romOff = 0xAC9700u + (s.dataIndex << 8) + static_cast<uint32_t>(index);
            }
            std::string name = (index < friendly.size() && !friendly[index].empty())
                                   ? friendly[index]
                                   : (std::string(s.buildId) + "_" + std::to_string(index));
            const std::string symbol = std::string(s.folder) + "/" + name;
            YAML::Node an;
            an["type"] = DkrYamlType(s);
            an["offset"] = romOff;
            an["size"] = size;
            an["dkr_type"] = static_cast<uint32_t>(s.type);
            an["symbol"] = symbol;
            Companion::Instance->AddSubFileAssetAbsolute(an, symbol);
            // Record (assetId, path) for the typed-pipeline manifest. assetId packs the
            // section data index into the high bits so per-section runtime resolvers decode
            // it without colliding across sections.
            tableData->entries.push_back(
                { static_cast<uint32_t>(s.dataIndex) << 20 | static_cast<uint32_t>(index), symbol });
            totalAssets++;
        };

        if (s.tableIndex >= 0 && s.tableIndex < (int)count) {
            const uint32_t tStart = dataBase + ftOff[s.tableIndex];
            const size_t tMaxBytes = ftOff[s.tableIndex + 1] - ftOff[s.tableIndex];
            const auto offs = ParseOffsets(buffer, tStart, s.tableType, tMaxBytes);
            for (size_t i = 0; i + 1 < offs.size(); i++) {
                emitAsset(dStart + offs[i], offs[i + 1] - offs[i], i);
            }
        } else {
            emitAsset(dStart, dSize, 0);
        }
    }

    // Emit each paired _TABLE section as a single resource so asset_table_load can run
    // without the assets.bin blob (Phase 3 no-glob cutover). assetId = (tableIndex << 20)|0.
    // Static resources match the blob byte-for-byte and stay correct for in-place asset
    // edits; runtime derivation-from-sizes is a future enhancement for count-changing mods.
    for (const auto& s : SECTIONS) {
        if (s.tableIndex < 0 || s.tableIndex >= (int)count) continue;
        const uint32_t tStart = dataBase + ftOff[s.tableIndex];
        const uint32_t tSize = ftOff[s.tableIndex + 1] - ftOff[s.tableIndex];
        if ((size_t)tStart + tSize > buffer.size() || tSize == 0) continue;
        const std::string symbol = std::string(s.folder) + "/" + std::string(s.buildId) + "_TABLE";
        YAML::Node an;
        an["type"] = "DKR:ASSET";
        an["offset"] = tStart;
        an["size"] = tSize;
        an["dkr_type"] = static_cast<uint32_t>(Torch::ResourceType::Blob);
        an["symbol"] = symbol;
        Companion::Instance->AddSubFileAssetAbsolute(an, symbol);
        tableData->entries.push_back({ static_cast<uint32_t>(s.tableIndex) << 20 | 0u, symbol });
        totalAssets++;
    }

    // The legacy assets.bin / assets.lut.bin glob is no longer emitted: the PC
    // runtime (AssetHelper.cpp::EnsureLoaded) rebuilds that virtual layout in RAM
    // straight from the per-asset resources via the aDKRAssetTable manifest, so
    // the per-asset files are the single source of truth and a single edited
    // resource is picked up on the next extract without re-packing a blob.

    SPDLOG_INFO("[DKR] Dispatched {} assets across {} sections ({} manifest entries).", totalAssets,
                sizeof(SECTIONS) / sizeof(SECTIONS[0]), tableData->entries.size());
    return tableData;
}

ExportResult AssetTableBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                                 std::string& entryName, YAML::Node& node,
                                                 std::string* replacement) {
    auto data = std::static_pointer_cast<AssetTableData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::Blob, 0);

    // Manifest blob the runtime resolver (DkrResourceResolver) loads to map a decomp
    // section table index to a typed resource path. Layout: u32 payloadSize, u32 count,
    // then per entry { u32 assetId, s32 pathLen, char path[pathLen] } (assetId packs
    // (section<<20)|localIndex). BinaryWriter.Write(std::string) emits len + chars.
    size_t payloadSize = 4; // u32 count
    for (const auto& e : data->entries) {
        payloadSize += 4 + 4 + e.path.size();
    }
    writer.Write(static_cast<uint32_t>(payloadSize));
    writer.Write(static_cast<uint32_t>(data->entries.size()));
    for (const auto& e : data->entries) {
        writer.Write(e.assetId);
        writer.Write(e.path);
    }
    writer.Finish(write);
    return OffsetEntry{ 0 };
}
