#include "MenuTextFactory.h"

#include "DkrTextTables.h"
#include "MenuTextBuildIds.h"

#include "Companion.h"
#include "../ResourceType.h"
#include "utils/Decompressor.h"
#include "lib/binarytools/BinaryWriter.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

namespace {

// --- DKRText codec (ported from dkr_assets_tool_src/text/dkrText.cpp) ---
// ASCII text is stored 1 byte per char. Japanese text uses the DKRJP encoding: a byte
// with the high bit set marks a 2-byte char whose index is ((b0 & 0x7F) << 8) | b1 into
// the 256-entry font table (DkrJpCharacters). A string is ASCII unless any byte has the
// high bit set.

std::string DkrTextDecode(const uint8_t* data, size_t avail) {
    // First pass: find the null terminator and detect any DKRJP (high-bit) byte.
    bool hasJp = false;
    size_t n = 0;
    while (n < avail && data[n] != 0) {
        uint8_t c = data[n++];
        if (c & 0x80) {
            hasJp = true;
            n++; // skip the low byte of the 2-byte DKRJP char
        }
    }
    if (!hasJp) {
        return std::string(reinterpret_cast<const char*>(data), n);
    }
    const auto& table = DkrJpCharacters();
    std::string out;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = data[i];
        if (c & 0x80) {
            size_t idx = (static_cast<size_t>(c & 0x7F) << 8) | data[++i];
            if (idx < table.size()) {
                out += table[idx];
            }
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

// Pull the next UTF-8 codepoint from `text` at `off`; advance `off` past it.
std::string NextUtf8Char(const std::string& text, size_t& off) {
    uint8_t b0 = static_cast<uint8_t>(text[off]);
    size_t len = 1;
    if ((b0 & 0xE0) == 0xC0) len = 2;
    else if ((b0 & 0xF0) == 0xE0) len = 3;
    else if ((b0 & 0xF8) == 0xF0) len = 4;
    std::string ch = text.substr(off, len);
    off += len;
    return ch;
}

std::vector<uint8_t> DkrTextEncode(const std::string& text) {
    bool hasNonAscii = std::any_of(text.begin(), text.end(), [](unsigned char c) { return c > 127; });
    if (!hasNonAscii) {
        return std::vector<uint8_t>(text.begin(), text.end());
    }
    const auto& table = DkrJpCharacters();
    std::vector<uint8_t> bytes;
    if (text.empty()) {
        bytes.push_back(0x80);
        bytes.push_back(0x00);
        return bytes;
    }
    size_t off = 0;
    while (off < text.size()) {
        std::string ch = NextUtf8Char(text, off);
        int idx = -1;
        for (size_t i = 0; i < table.size(); i++) {
            if (table[i] == ch) {
                idx = static_cast<int>(i);
                break;
            }
        }
        if (idx < 0) {
            // Not in the JP table: keep plain ASCII control bytes, drop anything else.
            if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) <= 127) {
                bytes.push_back(static_cast<uint8_t>(ch[0]));
            }
            continue;
        }
        bytes.push_back(static_cast<uint8_t>(0x80 | ((idx >> 8) & 0xFF)));
        bytes.push_back(static_cast<uint8_t>(idx & 0xFF));
    }
    return bytes;
}

inline uint32_t RdBe32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
inline void WrBe32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

// The offset table has N entries; the strings follow at byte N*4. N is recoverable from
// the resource alone because the first non-null offset always equals N*4 (the build lays
// strings out immediately after the table). Null entries (0xFFFFFFFF) carry no string.
size_t MenuTextEntryCount(const std::vector<uint8_t>& bytes) {
    size_t maxWords = bytes.size() / 4;
    for (size_t i = 0; i < maxWords; i++) {
        uint32_t v = RdBe32(bytes.data() + i * 4);
        if (v != 0xFFFFFFFF) {
            size_t n = v / 4;
            return std::min(n, maxWords);
        }
    }
    return maxWords; // all-null table
}

// Decode the resource into one optional<string> per offset slot, in order.
std::vector<std::optional<std::string>> DecodeMenuText(const std::vector<uint8_t>& bytes) {
    std::vector<std::optional<std::string>> out;
    size_t count = MenuTextEntryCount(bytes);
    out.reserve(count);
    for (size_t i = 0; i < count; i++) {
        uint32_t v = RdBe32(bytes.data() + i * 4);
        if (v == 0xFFFFFFFF || static_cast<size_t>(v) >= bytes.size()) {
            out.emplace_back(std::nullopt);
            continue;
        }
        out.emplace_back(DkrTextDecode(bytes.data() + v, bytes.size() - v));
    }
    return out;
}

// Re-encode the slot list back into the on-disk layout (mirrors buildMenuText: N offsets,
// then strings each null-terminated, 8-byte aligned total). Zero-initialised so the null
// terminators and the alignment padding are already 0.
std::vector<uint8_t> EncodeMenuText(const std::vector<std::optional<std::string>>& entries) {
    size_t count = entries.size();
    std::vector<std::vector<uint8_t>> enc(count);
    size_t totalStr = 0;
    for (size_t i = 0; i < count; i++) {
        if (entries[i].has_value()) {
            enc[i] = DkrTextEncode(*entries[i]);
            totalStr += enc[i].size() + 1; // +1 null terminator
        }
    }
    size_t outSize = (count * 4) + totalStr;
    outSize = (outSize + 7) & ~size_t(7); // align 8

    std::vector<uint8_t> out(outSize, 0);
    size_t cur = count * 4;
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].has_value()) {
            WrBe32(out.data() + i * 4, 0xFFFFFFFF);
            continue;
        }
        WrBe32(out.data() + i * 4, static_cast<uint32_t>(cur));
        if (!enc[i].empty()) {
            std::memcpy(out.data() + cur, enc[i].data(), enc[i].size());
        }
        cur += enc[i].size() + 1; // leave the trailing null (already 0)
    }
    return out;
}

std::string LanguageFromSymbol(const std::string& symbol) {
    std::string low = symbol;
    std::transform(low.begin(), low.end(), low.begin(), [](unsigned char c) { return std::tolower(c); });
    if (low.find("french") != std::string::npos) return "French";
    if (low.find("german") != std::string::npos) return "German";
    if (low.find("japanese") != std::string::npos || low.find("jp") != std::string::npos) return "Japanese";
    return "English";
}

} // namespace

std::optional<std::shared_ptr<IParsedData>> MenuTextFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    auto [_, segment] = Decompressor::AutoDecode(node, buffer);
    auto data = std::make_shared<MenuTextData>();
    data->bytes.assign(segment.data, segment.data + segment.size);
    return data;
}

ExportResult MenuTextBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                            std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<MenuTextData>(raw);

    LUS::BinaryWriter writer;
    BaseExporter::WriteHeader(writer, Torch::ResourceType::DKRMenuText, 0);
    writer.Write(static_cast<uint32_t>(data->bytes.size()));
    if (!data->bytes.empty()) {
        writer.Write((char*) data->bytes.data(), data->bytes.size());
    }
    writer.Finish(write);
    return std::nullopt;
}

ExportResult MenuTextModdingExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw,
                                             std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto data = std::static_pointer_cast<MenuTextData>(raw);
    std::string symbol = node["symbol"] ? node["symbol"].as<std::string>() : entryName;
    *replacement += ".yaml";

    const auto entries = DecodeMenuText(data->bytes);
    const auto& ids = MenuTextBuildIds();

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << symbol << YAML::Value;
    out << YAML::BeginMap;
    out << YAML::Key << "language" << YAML::Value << LanguageFromSymbol(symbol);
    out << YAML::Key << "sections" << YAML::Value << YAML::BeginMap;
    for (size_t i = 0; i < entries.size(); i++) {
        if (!entries[i].has_value()) {
            continue; // null slot -> omit; parse_modding recreates the 0xFFFFFFFF entry
        }
        std::string key = (i < ids.size()) ? ids[i] : ("ASSET_MENU_TEXT_UNKNOWN_" + std::to_string(i));
        out << YAML::Key << key << YAML::Value << *entries[i];
    }
    out << YAML::EndMap;  // sections
    out << YAML::EndMap;  // symbol value
    out << YAML::EndMap;  // root
    write.write(out.c_str(), out.size());
    return std::nullopt;
}

std::optional<std::shared_ptr<IParsedData>> MenuTextFactory::parse_modding(std::vector<uint8_t>& buffer,
                                                                            YAML::Node& node) {
    YAML::Node root;
    try {
        std::string text((char*) buffer.data(), buffer.size());
        root = YAML::Load(text);
    } catch (YAML::ParserException& e) {
        SPDLOG_ERROR("MenuText modding parse failed: {}", e.what());
        return std::nullopt;
    }

    auto info = root.begin()->second; // {language: ..., sections: {...}}
    auto sections = info["sections"];
    const auto& ids = MenuTextBuildIds();

    // One slot per build-id in canonical order (the v77 slot count). A missing key is a
    // null (0xFFFFFFFF) slot, which is how all-null languages (e.g. japanese_menu_text)
    // round-trip: every key absent -> every slot null.
    std::vector<std::optional<std::string>> entries(ids.size());
    for (size_t i = 0; i < ids.size(); i++) {
        auto v = sections[ids[i]];
        if (v && !v.IsNull()) {
            entries[i] = v.as<std::string>();
        }
    }

    auto data = std::make_shared<MenuTextData>();
    data->bytes = EncodeMenuText(entries);
    return data;
}
