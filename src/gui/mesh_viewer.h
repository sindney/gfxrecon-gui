#pragma once
#include "analysis/mesh.h"
#include <memory>
namespace gfx {
class MeshViewer {
public:
    MeshViewer(); ~MeshViewer();
    void SetData(std::shared_ptr<MeshData> data);
    void Draw();
    void Release();
    size_t ScreenVertexCount() const;
    MeshPosition ScreenVertex(size_t index) const;
    size_t InputVertexCount() const;
    const std::string& Diagnostic() const;
    std::array<uint8_t,4> PreviewCenter() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
