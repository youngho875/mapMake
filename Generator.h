#pragma once
#include <atomic>
#include <filesystem>
#include <string>
#include <vector>
namespace mm {
namespace fs = std::filesystem;
struct Layer 
{
    fs::path local, relative;
    std::string name, type, epsg = "4326", featureId, issue;
    std::vector<std::string> fields;
    std::string sldFile; // Packaged SLD relative to mapfile (MapServer 8.2+).
    std::string rgb; // Empty: default color; nonempty: explicit per-layer override.
};

struct ScanResult 
{
    std::vector<Layer> layers; 
    std::vector<std::string> warnings; 
};

struct Settings 
{
    bool wms=true, wfs=false, wmts=true, sld=true;
    std::string mapEpsg="4326", mapUnits="DD", extent="124 33 132 39", rgb="40 110 190";
    std::string serverRoot="/srv/maps/data", mapPath="/srv/maps/mapserver.map";
    std::string internalUrl="http://127.0.0.1/cgi-bin/mapserv", publicUrl="http://localhost/cgi-bin/mapserv", cache="/var/cache/mapcache";
};

struct Artifact 
{ 
    fs::path name; 
    std::string content;
};

std::vector<std::string> ParseExtensions(std::string text);
ScanResult Scan(const fs::path& root, const std::string& extensions, bool recursive, const std::atomic_bool* cancel=nullptr);
std::string NormalizeRgb(const std::string& value);
std::string RgbHex(const std::string& value);
void ValidateEpsg(const std::string& value);
std::vector<Artifact> Build(const Settings& settings, const std::vector<Layer>& layers);
fs::path SaveBatch(const fs::path& output, const std::vector<Artifact>& artifacts);
}
