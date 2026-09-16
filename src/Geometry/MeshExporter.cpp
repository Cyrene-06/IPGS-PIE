// ============================================================================
// MeshExporter implementation
// ============================================================================
#include "Geometry/MeshExporter.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>

namespace {

QString buildMtl(const std::vector<ObjMaterial>& materials) {
    QString result;
    QTextStream stream(&result);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(4);
    stream << "# PlantSim materials\n";
    for (const ObjMaterial& material : materials) {
        stream << "newmtl " << material.name << '\n'
               << "Ka " << material.diffuse.x() * 0.2f << ' '
               << material.diffuse.y() * 0.2f << ' '
               << material.diffuse.z() * 0.2f << '\n'
               << "Kd " << material.diffuse.x() << ' '
               << material.diffuse.y() << ' '
               << material.diffuse.z() << '\n'
               << "Ks " << material.specular.x() << ' '
               << material.specular.y() << ' '
               << material.specular.z() << '\n'
               << "Ns " << material.shininess << '\n'
               << "d 1.0\nillum 2\n";
        if (material.doubleSided) {
            stream << "# double-sided\n";
        }
        stream << '\n';
    }
    return result;
}

} // namespace

bool MeshExporter::serializeObj(const QString& baseName,
                                const std::vector<ObjMaterial>& materials,
                                const std::vector<ObjMeshGroup>& groups,
                                QString* objText,
                                QString* mtlText,
                                QString* error) {
    if (!objText || !mtlText) {
        if (error) *error = QStringLiteral("OBJ and MTL output pointers are required.");
        return false;
    }
    if (materials.empty()) {
        if (error) *error = QStringLiteral("At least one material is required.");
        return false;
    }

    QString obj;
    QTextStream stream(&obj);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);
    const QString normalizedBaseName = baseName.trimmed().isEmpty()
        ? QStringLiteral("plantsim_scene") : baseName.trimmed();
    stream << "# PlantSim plant model\n"
           << "mtllib " << normalizedBaseName << ".mtl\n";

    std::uint32_t vertexOffset = 0;
    bool wroteMesh = false;
    for (const ObjMeshGroup& group : groups) {
        if (!group.mesh || !group.mesh->isValid()) continue;
        wroteMesh = true;
        const SurfaceMesh& mesh = *group.mesh;
        stream << "g " << (group.comment.isEmpty() ? QStringLiteral("mesh")
                                                    : group.comment) << '\n';
        for (const Vec3& p : mesh.positions) {
            stream << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
        }
        for (const Vec3& n : mesh.normals) {
            stream << "vn " << n.x() << ' ' << n.y() << ' ' << n.z() << '\n';
        }
        int activeMaterial = -1;
        const std::size_t triangleCount = mesh.indices.size() / 3;
        for (std::size_t tri = 0; tri < triangleCount; ++tri) {
            int material = group.uniformMaterial;
            if (group.faceMaterials && tri < group.faceMaterials->size()) {
                material = (*group.faceMaterials)[tri];
            }
            material = std::max(0, std::min(static_cast<int>(materials.size()) - 1,
                                            material));
            if (material != activeMaterial) {
                stream << "usemtl " << materials[material].name << '\n';
                activeMaterial = material;
            }
            const std::uint32_t a = vertexOffset + mesh.indices[tri * 3] + 1;
            const std::uint32_t b = vertexOffset + mesh.indices[tri * 3 + 1] + 1;
            const std::uint32_t c = vertexOffset + mesh.indices[tri * 3 + 2] + 1;
            stream << "f " << a << "//" << a << ' '
                   << b << "//" << b << ' '
                   << c << "//" << c << '\n';
        }
        vertexOffset += static_cast<std::uint32_t>(mesh.positions.size());
    }
    if (!wroteMesh) {
        if (error) *error = QStringLiteral("No valid mesh group is available for OBJ export.");
        return false;
    }
    *objText = std::move(obj);
    *mtlText = buildMtl(materials);
    return true;
}

bool MeshExporter::saveObj(const QString& objPath,
                           const std::vector<ObjMaterial>& materials,
                           const std::vector<ObjMeshGroup>& groups,
                           QString* error) {
    const QFileInfo objInfo(objPath);
    const QString mtlPath = objInfo.absolutePath() + QStringLiteral("/") +
                            objInfo.completeBaseName() + QStringLiteral(".mtl");
    QString objText;
    QString mtlText;
    if (!serializeObj(objInfo.completeBaseName(), materials, groups,
                      &objText, &mtlText, error)) {
        return false;
    }

    QSaveFile mtlFile(mtlPath);
    if (!mtlFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        mtlFile.write(mtlText.toUtf8()) < 0 || !mtlFile.commit()) {
        if (error) *error = QStringLiteral("Cannot write MTL: %1").arg(mtlPath);
        return false;
    }

    QSaveFile objFile(objPath);
    if (!objFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        objFile.write(objText.toUtf8()) < 0 || !objFile.commit()) {
        if (error) *error = QStringLiteral("Cannot write OBJ: %1").arg(objPath);
        QFile::remove(mtlPath);
        return false;
    }
    return true;
}
