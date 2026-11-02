namespace {
// Read only raster headers before creating the mosaic workspace.
TileOptions PreflightRasterTiles(const std::vector<MergeGroup>& groups,
    const TileOptions& requested, std::atomic_bool& cancel,
    const std::function<void(const std::string&)>& progress,
    std::vector<std::string>& messages)
{
    if (requested.minZoom < 0 || requested.minZoom > 24 || requested.maxTiles == 0
        || (!requested.autoZoom && (requested.maxZoom < requested.minZoom || requested.maxZoom > 24)))
        throw std::runtime_error("Invalid zoom range or tile limit.");
    struct Bounds { double west, south, east, north; };
    std::vector<Bounds> bounds;
    double finestPixel = 0;
    int desiredZoom = requested.minZoom;
    size_t inspected = 0, totalFiles = 0;
    for (const auto& group : groups) totalFiles += group.inputs.size();
    for (const auto& group : groups)
    {
        Bounds area{World, World, -World, -World};
        for (const auto& layer : group.inputs)
        {
            if (cancel) return requested;
            ++inspected;
            if (inspected == 1 || inspected % 100 == 0 || inspected == totalFiles)
                progress("Checking raster headers: " + std::to_string(inspected)
                    + "/" + std::to_string(totalFiles));
            auto source = Own(GDALOpenEx(layer.local.u8string().c_str(),
                GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!source) continue; // The preparation stage reports skipped inputs.
            char** options = nullptr;
            options = CSLSetNameValue(options, "DST_SRS", "EPSG:3857");
            if (!source->GetSpatialRef() && requested.fallbackEpsg > 0)
                options = CSLSetNameValue(options, "SRC_SRS",
                    ("EPSG:" + std::to_string(requested.fallbackEpsg)).c_str());
            void* transform = GDALCreateGenImgProjTransformer2(source.get(), nullptr, options);
            CSLDestroy(options);
            if (!transform) continue;
            double geo[6], extent[4];
            int width = 0, height = 0;
            const auto error = GDALSuggestedWarpOutput2(source.get(), GDALGenImgProjTransform,
                transform, geo, &width, &height, extent, 0);
            GDALDestroyGenImgProjTransformer(transform);
            if (error != CE_None || width <= 0 || height <= 0) continue;
            bool finite = true;
            for (double value : extent) finite = finite && std::isfinite(value);
            if (!finite) continue;
            area.west = std::min(area.west, std::max(-World, extent[0]));
            area.south = std::min(area.south, std::max(-World, extent[1]));
            area.east = std::max(area.east, std::min(World, extent[2]));
            area.north = std::max(area.north, std::min(World, extent[3]));
            const double pixel = std::max(std::abs(geo[1]), std::abs(geo[5]));
            if (std::isfinite(pixel) && pixel > 0)
                finestPixel = finestPixel > 0 ? std::min(finestPixel, pixel) : pixel;
        }
        if (area.west >= area.east || area.south >= area.north) continue;
        bounds.push_back(area);
        const double latitude = atan(sinh((area.south + area.north) * 0.5
            / (World / 3.141592653589793))) * 180 / 3.141592653589793;
        if (requested.autoZoom)
            desiredZoom = std::max(desiredZoom, ZoomForScale(requested.targetScale, latitude));
    }
    if (bounds.empty()) return requested;

    auto countTiles = [&](int maximum)
    {
        uint64_t total = 0;
        for (const auto& area : bounds)
        {
            for (int zoom = requested.minZoom; zoom <= maximum; ++zoom)
            {
                const int64_t count = 1LL << zoom;
                const double span = 2 * World / count;
                auto index = [&](double coordinate)
                {
                    return std::clamp(static_cast<int64_t>(floor(coordinate)), int64_t(0), count - 1);
                };
                const auto left = index((area.west + World) / span);
                const auto right = index(std::nextafter((area.east + World) / span, -INFINITY));
                const auto top = index((World - area.north) / span);
                const auto bottom = index(std::nextafter((World - area.south) / span, -INFINITY));
                total += uint64_t(right - left + 1) * uint64_t(bottom - top + 1);
            }
        }
        return total;
    };

    TileOptions effective = requested;
    int maximum = requested.autoZoom ? desiredZoom : requested.maxZoom;
    if (requested.autoZoom && finestPixel > 0)
    {
        const int nativeZoom = std::clamp(static_cast<int>(ceil(log2(2 * World / 256 / finestPixel))), 0, 24);
        if (requested.minZoom > nativeZoom)
            throw std::runtime_error("Minimum zoom exceeds source resolution. Reduce minimum zoom to " + std::to_string(nativeZoom));
        maximum = std::min(maximum, nativeZoom);
    }
    if (requested.autoZoom)
    {
        while (maximum > requested.minZoom && countTiles(maximum) > requested.maxTiles)
            --maximum;
    }
    const uint64_t finalCount = countTiles(maximum);
    const std::string summary = "PREFLIGHT: scale denominator=" + Number(requested.targetScale)
        + ", requested max zoom=" + std::to_string(requested.autoZoom ? desiredZoom : requested.maxZoom)
        + ", selected zoom=" + std::to_string(requested.minZoom) + ".." + std::to_string(maximum)
        + ", estimated tiles=" + std::to_string(finalCount)
        + ", limit=" + std::to_string(requested.maxTiles);
    progress(summary);
    messages.push_back(summary);
    if (finalCount > requested.maxTiles)
        throw std::runtime_error(summary + ". Reduce maximum/minimum zoom or select a smaller area. Mosaic preparation has not started.");
    if (requested.autoZoom)
    {
        effective.autoZoom = false;
        effective.maxZoom = maximum;
    }
    return effective;
}
}
