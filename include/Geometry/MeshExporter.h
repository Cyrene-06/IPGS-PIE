// ============================================================================
// MeshExporter - 第8周：带材质的 OBJ 模型导出
// ----------------------------------------------------------------------------
// 把枝干 / 叶片等多个 SurfaceMesh 合并写入一个 Wavefront OBJ 文件，
// 同时生成配套 MTL 材质库：每个网格组可按三角形指定材质索引，
// OBJ 索引在多网格间自动累加偏移。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include <QString>

#include "Common/MathTypes.h"
#include "Geometry/MarchingCubes.h"

struct ObjMaterial {
    QString name;
    Vec3 diffuse = Vec3(0.8f, 0.8f, 0.8f);   // Kd
    Vec3 specular = Vec3(0.04f, 0.04f, 0.04f); // Ks
    float shininess = 12.0f;                  // Ns
    bool doubleSided = false;                 // 仅写入注释，供下游导入器参考
};

struct ObjMeshGroup {
    const SurfaceMesh* mesh = nullptr;
    QString comment;                 // 写入 g 分组名
    int uniformMaterial = 0;         // faceMaterials 为空时整组使用的材质
    const std::vector<std::uint16_t>* faceMaterials = nullptr; // 每三角形材质
};

class MeshExporter {
public:
    // Serialize a complete OBJ/MTL pair for browser downloads or in-memory tests.
    static bool serializeObj(const QString& baseName,
                             const std::vector<ObjMaterial>& materials,
                             const std::vector<ObjMeshGroup>& groups,
                             QString* objText,
                             QString* mtlText,
                             QString* error = nullptr);

    // 写出 objPath 及同名 .mtl。materials 至少包含一个材质。
    static bool saveObj(const QString& objPath,
                        const std::vector<ObjMaterial>& materials,
                        const std::vector<ObjMeshGroup>& groups,
                        QString* error = nullptr);
};
