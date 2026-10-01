#pragma once

#include "../BaseFactory.h"

// DKR menu text (ASSET_MENU_TEXT, FixedTable section 8). One resource per language
// (English/French/German/Japanese). The on-disk layout is a big-endian u32 offset
// table -- one slot per string, 0xFFFFFFFF = null -- followed by the DKRText-encoded,
// null-terminated strings, the whole blob 8-byte aligned. The decomp reads this slice
// straight via asset_load (menu.c::load_menu_text).
//
// The factory keeps the raw bytes for the Binary exporter so a normal extraction is
// byte-identical to the legacy blob; the DKRText decode/re-encode runs only on the
// modding round-trip (Modding exporter + parse_modding), which is the only path that
// can introduce divergence. YAML keys are the 192 canonical v77 build-ids.
struct MenuTextData : public IParsedData {
    std::vector<uint8_t> bytes;
};

class MenuTextBinaryExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MenuTextModdingExporter : public BaseExporter {
public:
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MenuTextFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& node) override;
    std::optional<std::shared_ptr<IParsedData>> parse_modding(std::vector<uint8_t>& buffer, YAML::Node& node) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { REGISTER(Binary, MenuTextBinaryExporter) REGISTER(Modding, MenuTextModdingExporter) };
    }
    bool SupportModdedAssets() override { return true; }
};
