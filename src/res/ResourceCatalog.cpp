#include "res/ResourceCatalog.h"
#include "res/ManifestReader.h"

#include <iostream>

namespace {

constexpr const char* kManifestPath = "assets/manifests/textures.txt";

ResourceCatalogData FallbackCatalog(const std::string& reason) {
    ResourceCatalogData data;
    for (const auto& e : kPreloadTextures) data.preload.push_back({e.key, e.path});
    for (const auto& e : kAsyncTextures)   data.async.push_back({e.key, e.path});
    for (const char* name : kV2IconNames)  data.icons.push_back(name);
    data.source = "built-in fallback";
    std::cout << "[ResourceCatalog] " << reason << "，回退内置清单 ("
              << data.async.size() << " async textures)" << std::endl;
    return data;
}

ResourceCatalogData LoadCatalog() {
    std::vector<ManifestLine> lines;
    std::string error;
    if (!ReadManifest(kManifestPath, lines, &error))
        return FallbackCatalog(error);

    ResourceCatalogData data;
    for (const auto& line : lines) {
        if (line.section == "preload")     data.preload.push_back({line.key, line.value});
        else if (line.section == "async")  data.async.push_back({line.key, line.value});
        else if (line.section == "icons")  data.icons.push_back(line.key);
        else if (!line.section.empty())
            return FallbackCatalog("未知段 [" + line.section + "]");
    }

    // 三段都必须存在且非空，否则视为清单不完整（宁可回退，也不要半份资源）
    if (data.preload.empty() || data.async.empty() || data.icons.empty())
        return FallbackCatalog("清单段不完整");

    for (const auto& e : data.async) {
        if (e.key.empty() || e.path.empty())
            return FallbackCatalog("存在空键或空路径的条目");
    }
    for (const auto& e : data.preload) {
        if (e.key.empty() || e.path.empty())
            return FallbackCatalog("预载段存在空键或空路径");
    }

    data.source = "manifest";
    std::cout << "[ResourceCatalog] loaded from " << kManifestPath << " ("
              << data.async.size() << " async textures, "
              << data.icons.size() << " icons)" << std::endl;
    return data;
}

} // namespace

const ResourceCatalogData& GetResourceCatalog() {
    static const ResourceCatalogData data = LoadCatalog();
    return data;
}
