#pragma once

#include "../BaseFactory.h"

// Typed DKR object-header (ASSET_OBJECTS, section 34). Parses the big-endian N64 bytes
// (header scalars + offset fields + the trailed modelIds/objectParticles/vehiclePartIds
// arrays) and re-emits them host-little-endian, byte-identical to what the runtime's
// dkr_objectheader_to_host produced in-place. Offset fields stay file-relative; the
// decomp rebases them at runtime (load_object_header). This retires the runtime swap for
// this section (Phase 1 of the typed-pipeline migration, plan: lazy-painting-muffin.md).
class DKRObjectHeaderBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class ObjectHeaderFactory : public BaseFactory {
public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;

    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return {
            REGISTER(Binary, DKRObjectHeaderBinaryExporter)
        };
    }
};
