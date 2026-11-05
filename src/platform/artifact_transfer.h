#pragma once
#include <atomic>
#include <filesystem>
#include <string>

namespace gfx {
struct ArtifactTransferStats {
    std::string codec="raw",integrity="framing";
    uint64_t wireBytes=0,rawBytes=0;
};
// Verify/decode the transport envelope into a private temporary file. Legacy
// raw streams are parsed by the caller; compressed data is never published here.
bool DecodeArtifactTransfer(const std::filesystem::path& wire,const std::filesystem::path& decoded,
                            ArtifactTransferStats& stats,const std::atomic_bool* cancel=nullptr);
}
