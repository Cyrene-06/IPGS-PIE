#include <QCoreApplication>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

#include "Engine/SimulationEngine.h"

namespace {

bool closeEnough(float a, float b, float tolerance = 1.0e-4f) {
    return std::abs(a - b) <= tolerance;
}

int fail(const QString& message) {
    QTextStream(stderr) << "ScenePersistenceRegression: " << message << Qt::endl;
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    SimulationEngine engine;
    engine.setLightIntensity(0.31f);
    engine.setPhototropismWeight(0.72f);
    engine.setGrowthSpeed(2.5f);
    engine.stepGrowth(0.25f);

    const float archivedAge = engine.plantModel().age;
    const QJsonObject archivedPlant = engine.plantModel().toJson();
    const QJsonObject preset = engine.createPreset();
    if (preset.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.preset")) {
        return fail(QStringLiteral("preset schema missing"));
    }

    QString error;
    engine.setLightIntensity(0.95f);
    if (!engine.applyPreset(preset, &error)) return fail(error);
    if (!closeEnough(engine.environment().lightIntensity, 0.31f) ||
        !closeEnough(engine.environment().phototropismWeight, 0.72f) ||
        !closeEnough(engine.growthTimeline().speed(), 2.5f)) {
        return fail(QStringLiteral("preset did not restore environment or timeline parameters"));
    }

    const QJsonObject scene = engine.createSceneArchive();
    if (scene.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.scene")) {
        return fail(QStringLiteral("scene schema missing"));
    }
    engine.setLightIntensity(0.88f);
    engine.resetGrowth(0.0f);
    if (!engine.restoreSceneArchive(scene, -1.0f, &error)) return fail(error);
    if (!closeEnough(engine.environment().lightIntensity, 0.31f) ||
        !closeEnough(engine.plantModel().age, archivedAge) ||
        engine.plantModel().toJson() != archivedPlant) {
        return fail(QStringLiteral("scene archive did not restore the exact current scene"));
    }
    if (!engine.restoreRecordedScene(archivedAge, &error) ||
        !closeEnough(engine.plantModel().age, archivedAge)) {
        return fail(QStringLiteral("recorded scene restore failed: %1").arg(error));
    }

    QTemporaryDir directory;
    if (!directory.isValid()) return fail(QStringLiteral("temporary directory unavailable"));
    const QString presetPath = directory.filePath(QStringLiteral("preset.json"));
    const QString scenePath = directory.filePath(QStringLiteral("scene.json"));
    const QString objPath = directory.filePath(QStringLiteral("plant.obj"));
    if (!engine.savePreset(presetPath, &error) || !engine.loadPreset(presetPath, &error) ||
        !engine.saveSceneArchive(scenePath, &error) || !engine.loadSceneArchive(scenePath, -1.0f, &error)) {
        return fail(QStringLiteral("file persistence failed: %1").arg(error));
    }
    if (!engine.exportSceneObj(objPath, &error)) return fail(error);
    QFile objFile(objPath);
    QFile mtlFile(directory.filePath(QStringLiteral("plant.mtl")));
    if (!objFile.open(QIODevice::ReadOnly) || !mtlFile.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("OBJ/MTL files were not created"));
    }
    const QByteArray obj = objFile.readAll();
    const QByteArray mtl = mtlFile.readAll();
    if (!obj.contains("mtllib plant.mtl") || !obj.contains("g branches") ||
        !obj.contains("g leaves") || !mtl.contains("newmtl bark") ||
        !mtl.contains("newmtl leaf_0")) {
        return fail(QStringLiteral("OBJ/MTL content is incomplete (mtllib=%1 branches=%2 leaves=%3 bark=%4 leafMaterial=%5)")
                        .arg(obj.contains("mtllib plant.mtl"))
                        .arg(obj.contains("g branches"))
                        .arg(obj.contains("g leaves"))
                        .arg(mtl.contains("newmtl bark"))
                        .arg(mtl.contains("newmtl leaf_0")));
    }

    const QJsonObject bundle = engine.createSceneExportBundle(&error);
    if (bundle.isEmpty() || !bundle.value(QStringLiteral("obj")).toString().contains(QStringLiteral("g leaves")) ||
        bundle.value(QStringLiteral("branchTriangles")).toInt() <= 0 ||
        bundle.value(QStringLiteral("leafTriangles")).toInt() <= 0) {
        return fail(QStringLiteral("browser export bundle is incomplete: %1").arg(error));
    }

    QTextStream(stdout) << "ScenePersistenceRegression passed" << Qt::endl;
    return 0;
}
