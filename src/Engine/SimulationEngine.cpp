// ============================================================================
// SimulationEngine - environment, plant state bridge, and growth time controller
// 第11周：向光性与向地性，环境资源状态传递
// ============================================================================
#include "Engine/SimulationEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "Algorithm/TurtleInterpreter.h"
#include "Geometry/LeafGenerator.h"
#include "Geometry/MeshExporter.h"

namespace {

void setPersistenceError(QString* error, const QString& value) {
    if (error) *error = value;
}

bool saveJsonObject(const QString& filePath, const QJsonObject& object, QString* error) {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setPersistenceError(error, QStringLiteral("Cannot open %1 for writing: %2")
                                       .arg(filePath, file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setPersistenceError(error, QStringLiteral("Cannot save %1: %2")
                                       .arg(filePath, file.errorString()));
        return false;
    }
    return true;
}

bool loadJsonObject(const QString& filePath, QJsonObject* object, QString* error) {
    if (!object) {
        setPersistenceError(error, QStringLiteral("JSON output pointer is null."));
        return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setPersistenceError(error, QStringLiteral("Cannot open %1: %2")
                                       .arg(filePath, file.errorString()));
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setPersistenceError(error, QStringLiteral("Invalid JSON in %1: %2")
                                       .arg(filePath, parseError.errorString()));
        return false;
    }
    *object = document.object();
    return true;
}

std::vector<ObjMaterial> sceneMaterials() {
    std::vector<ObjMaterial> materials;
    ObjMaterial bark;
    bark.name = QStringLiteral("bark");
    bark.diffuse = Vec3(0.45f, 0.30f, 0.20f);
    bark.specular = Vec3(0.05f, 0.04f, 0.03f);
    bark.shininess = 8.0f;
    materials.push_back(bark);

    const Vec3 leafColors[] = {
        Vec3(0.30f, 0.52f, 0.26f), Vec3(0.40f, 0.60f, 0.29f),
        Vec3(0.26f, 0.45f, 0.23f), Vec3(0.48f, 0.64f, 0.32f)
    };
    for (int index = 0; index < 4; ++index) {
        ObjMaterial leaf;
        leaf.name = QStringLiteral("leaf_%1").arg(index);
        leaf.diffuse = leafColors[index];
        leaf.specular = Vec3(0.10f, 0.12f, 0.08f);
        leaf.shininess = 24.0f;
        leaf.doubleSided = true;
        materials.push_back(leaf);
    }
    return materials;
}

GeneratedLeaves generateSceneLeaves(const PlantModel& plant) {
    LeafGenerationSettings settings;
    settings.seed = 20260816u;
    return LeafGenerator::generate(plant, settings);
}

std::vector<std::uint16_t> globalLeafMaterials(const GeneratedLeaves& leaves) {
    std::vector<std::uint16_t> result = leaves.faceMaterials;
    for (std::uint16_t& material : result) ++material;
    return result;
}

SurfaceMesh exportBranchMesh(const PlantModel& plant,
                             const MetaballFieldSettings& settings,
                             const SurfaceMesh& current) {
    if (current.isValid()) return current;
    MetaballField field;
    field.rebuildFromPlant(plant, settings);
    return MarchingCubes::extract(field.sampleGrid(0.08f, 2000000), field.isoThreshold());
}

}  // namespace

SimulationEngine::SimulationEngine(QObject* parent)
    : QObject(parent) {
    growthClock_.setParent(this);

    lSystem_ = LSystem::treePreset();
    plantProgram_ = lSystem_.generate(3);

    PlantRule rule;
    rule.angleDegrees = 23.0f;
    rule.length = 0.42f;
    rule.radius = 0.16f;
    rule.lengthScale = 0.9f;
    rule.radiusScale = 0.7f;

    plantModel_.id = 1;
    plantModel_.name = QStringLiteral("L-System Tree");
    plantModel_.species = QStringLiteral("Procedural Demo");
    plantModel_.age = 0.0f;
    plantModel_.lifeStage = PlantLifeStage::Seedling;
    plantModel_.growthState = PlantGrowthState::Paused;
    plantModel_.setRootNode(TurtleInterpreter().interpret(plantProgram_, rule));

    MetaballFieldSettings fieldSettings;
    fieldSettings.isoThreshold = 0.5f;
    fieldSettings.influenceScale = 2.0f;
    fieldSettings.jointSmoothness = 0.65f;
    metaballSettings_ = fieldSettings;
    metaballField_.rebuildFromPlant(plantModel_, fieldSettings);
    rebuildPhysicsModel();
    initialPlantSnapshot_ = plantModel_.toJson();
    growthData_.capture(plantModel_, true);

    connect(&growthClock_, &GrowthClock::tickProduced,
            this, &SimulationEngine::onGrowthTickProduced);
    connect(&growthClock_, &GrowthClock::logMessage,
            this, &SimulationEngine::onGrowthClockLog);
    connect(&growthClock_, &GrowthClock::modeChanged,
            this, [this](int) {
                emit growthLogMessage(QStringLiteral("Mode -> %1")
                    .arg(growthClock_.timeline().describeMode()));
            });

    qInfo().noquote() << QStringLiteral("L-System ready: iterations=3, symbols=%1, nodes=%2, max generation=%3")
                             .arg(plantProgram_.size())
                             .arg(static_cast<qulonglong>(plantNodeCount()))
                             .arg(plantRoot() ? plantRoot()->maxGeneration() : 0);
    qInfo().noquote() << QStringLiteral("Metaball field ready: node sources=%1, segment sources=%2, threshold=%3")
                             .arg(static_cast<qulonglong>(metaballField_.nodeSources().size()))
                             .arg(static_cast<qulonglong>(metaballField_.segmentSources().size()))
                             .arg(metaballField_.isoThreshold(), 0, 'f', 2);
    qInfo().noquote() << QStringLiteral("Growth timeline ready: thresholds=[%1, %2, %3], speed=%4x")
                             .arg(growthClock_.timeline().thresholds().seedlingEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().thresholds().vegetativeEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().thresholds().matureEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().speed(), 0, 'f', 1);

    rebuildPlantSurface();
}

// ============================================================================
// 访问器
// ============================================================================
const EnvironmentParams& SimulationEngine::environment() const { return environment_; }
EnvironmentParams& SimulationEngine::mutableEnvironment() { return environment_; }
const PlantModel& SimulationEngine::plantModel() const { return plantModel_; }
const PlantNode* SimulationEngine::plantRoot() const { return plantModel_.rootNode(); }
const QString& SimulationEngine::plantProgram() const { return plantProgram_; }
std::size_t SimulationEngine::plantNodeCount() const { return plantModel_.nodeCount(); }
const MetaballField& SimulationEngine::metaballField() const { return metaballField_; }

// ============================================================================
// 第11周：环境光照与向性槽
// ============================================================================
void SimulationEngine::setLightIntensity(float intensity) {
    environment_.lightIntensity = qBound(0.0f, intensity, 1.0f);
    if (!environment_.lightSources.empty()) {
        environment_.lightSources[0].intensity = environment_.lightIntensity;
    }
    qInfo().noquote() << QStringLiteral("Light intensity = %1").arg(environment_.lightIntensity, 0, 'f', 2);
    emit logMessage(QStringLiteral("Environment Updated"));
    emit environmentUpdated(environment_.lightIntensity);
}

void SimulationEngine::setPhototropismWeight(float weight) {
    environment_.phototropismWeight = qBound(0.0f, weight, 1.0f);
    qInfo().noquote() << QStringLiteral("Phototropism weight = %1").arg(environment_.phototropismWeight, 0, 'f', 2);
    emit tropismUpdated(environment_.phototropismWeight, environment_.gravitropismWeight);
}

void SimulationEngine::setGravitropismWeight(float weight) {
    environment_.gravitropismWeight = qBound(0.0f, weight, 1.0f);
    qInfo().noquote() << QStringLiteral("Gravitropism weight = %1").arg(environment_.gravitropismWeight, 0, 'f', 2);
    emit tropismUpdated(environment_.phototropismWeight, environment_.gravitropismWeight);
}

void SimulationEngine::setPhysicsEnabled(bool enabled) {
    if (physicsEnabled_ == enabled) return;
    physicsEnabled_ = enabled;
    rebuildPhysicsModel();
    emit growthLogMessage(enabled ? QStringLiteral("Plant PBD solver enabled.")
                                    : QStringLiteral("Plant PBD solver disabled."));
    emitPhysicsDebugSnapshot();
}

void SimulationEngine::setPhysicsDebugEnabled(bool enabled) {
    physicsDebugEnabled_ = enabled;
    emitPhysicsDebugSnapshot();
}

void SimulationEngine::setLightSourcePosition(int lightId, float x, float y, float z) {
    for (auto& light : environment_.lightSources) {
        if (light.id == lightId) {
            light.position = Vec3(x, y, z);
            if (light.type == LightType::Directional) {
                light.direction = (-light.position).normalized();
            }
            qInfo().noquote() << QStringLiteral("Light #%1 position set to (%2, %3, %4)")
                                     .arg(lightId).arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1);
            emit environmentUpdated(environment_.lightIntensity);
            break;
        }
    }
}

// ============================================================================
// 生长时间轴 slots
// ============================================================================
void SimulationEngine::startGrowth()  { growthClock_.start(); }
void SimulationEngine::pauseGrowth()  { growthClock_.pause(); }
void SimulationEngine::resumeGrowth() { growthClock_.resume(); }
void SimulationEngine::resetGrowth(float initialYears) {
    dynamicBranching_.reset();
    growthEvents_.clear();
    keyframes_.clear();
    growthData_.clear();
    nextAutoKeyframeAge_ = std::max(1.0f, initialYears + 1.0f);
    PlantModel restored;
    QString error;
    if (!initialPlantSnapshot_.isEmpty() && PlantModel::fromJson(initialPlantSnapshot_, &restored, &error)) {
        plantModel_ = std::move(restored);
    }
    const bool restoreExactInitialSnapshot = std::abs(initialYears) <= 1.0e-4f;
    if (restoreExactInitialSnapshot) restoringRecordedFrame_ = true;
    growthClock_.reset(initialYears);
    if (restoreExactInitialSnapshot) restoringRecordedFrame_ = false;
    rebuildPhysicsModel();
    rebuildMetaballField();
    rebuildPlantSurface();
    emit plantSurfaceUpdated(plantSurface_);
    captureGrowthFrameIfNeeded(true);
}
void SimulationEngine::setGrowthSpeed(float speed) {
    growthClock_.setSpeed(speed);
    // Confirm remote speed changes immediately instead of waiting for the next
    // realtime tick (which may never arrive while the simulation is paused).
    emit growthUpdated(buildReport(growthClock_.timeline().sample(growthClock_.timeline().currentAge())));
}
void SimulationEngine::stepGrowth(float deltaYears) { growthClock_.stepOnce(deltaYears); }

void SimulationEngine::seekGrowth(float age) {
    QString error;
    if (!restoreRecordedScene(age, &error)) {
        emit growthLogMessage(QStringLiteral("Replay restore failed: %1").arg(error));
    }
}

bool SimulationEngine::restoreRecordedScene(float age, QString* error) {
    const GrowthDataFrame* frame = growthData_.nearestSnapshot(std::max(0.0f, age));
    if (!frame) {
        setPersistenceError(error, QStringLiteral("No recorded plant snapshot is available."));
        return false;
    }
    growthClock_.pause();
    PlantModel restored;
    QString plantError;
    if (!PlantModel::fromJson(frame->plantState, &restored, &plantError)) {
        setPersistenceError(error, plantError);
        return false;
    }
    restoringRecordedFrame_ = true;
    plantModel_ = std::move(restored);
    dynamicBranching_.reset();
    growthClock_.reset(frame->age);
    environment_.time = frame->age;
    rebuildPhysicsModel();
    rebuildMetaballField();
    // Timeline scrubbing must stay responsive; use a coarser preview surface.
    // The next live tick rebuilds the normal-resolution surface (0.18m grid).
    rebuildPlantSurface(0.24f);
    restoringRecordedFrame_ = false;
    emit plantSurfaceUpdated(plantSurface_);
    emit growthUpdated(buildReport(growthClock_.timeline().sample(frame->age), true));
    emit growthLogMessage(QStringLiteral("Replay seek -> %1y").arg(frame->age, 0, 'f', 2));
    return true;
}

void SimulationEngine::jumpToGrowthStage(const QString& stage) {
    const QString normalized = stage.trimmed().toLower();
    const GrowthAxisThresholds thresholds = growthClock_.timeline().thresholds();
    float targetAge = 0.0f;
    if (normalized == QStringLiteral("seed") || normalized == QStringLiteral("seedling")) {
        targetAge = 0.0f;
    } else if (normalized == QStringLiteral("sprout") || normalized == QStringLiteral("vegetative")) {
        targetAge = thresholds.seedlingEndYear;
    } else if (normalized == QStringLiteral("growing")) {
        targetAge = thresholds.vegetativeEndYear;
    } else if (normalized == QStringLiteral("mature")) {
        // A representative point inside the mature stage rather than its start.
        targetAge = thresholds.vegetativeEndYear +
                    (thresholds.matureEndYear - thresholds.vegetativeEndYear) / 3.0f;
    } else if (normalized == QStringLiteral("aging") || normalized == QStringLiteral("completed")) {
        targetAge = thresholds.matureEndYear;
    } else {
        return;
    }
    seekGrowth(targetAge);
}

void SimulationEngine::requestGrowthData() {
    // Charts consume every metric frame, but replay state remains in the engine
    // and is sent only after an explicit seek. Avoid serializing checkpoint
    // skeletons merely because a browser connected.
    QJsonObject data = growthData_.metricsToJson();
    const GrowthAxisThresholds thresholds = growthClock_.timeline().thresholds();
    data.insert("keyStages", QJsonArray{
        QJsonObject{{"key", "seedling"}, {"label", "Seedling"}, {"age", 0.0}},
        QJsonObject{{"key", "vegetative"}, {"label", "Vegetative"}, {"age", static_cast<double>(thresholds.seedlingEndYear)}},
        QJsonObject{{"key", "mature"}, {"label", "Mature"}, {"age", static_cast<double>(thresholds.vegetativeEndYear)}},
        QJsonObject{{"key", "completed"}, {"label", "Completed"}, {"age", static_cast<double>(thresholds.matureEndYear)}}
    });
    emit growthDataAvailable(data);
}

// ============================================================================
// 内部槽
// ============================================================================
void SimulationEngine::onGrowthTickProduced(const GrowthSample& sample) {
    // seekGrowth() has already restored an exact PlantModel snapshot. Do not
    // apply the analytical growth scale again or the replayed geometry drifts.
    if (restoringRecordedFrame_) {
        environment_.time = sample.age;
        emit growthUpdated(buildReport(sample));
        return;
    }
    const float previousAge = plantModel_.age;
    if (sample.age + 1.0e-4f < previousAge && !initialPlantSnapshot_.isEmpty()) {
        PlantModel restored;
        QString error;
        if (PlantModel::fromJson(initialPlantSnapshot_, &restored, &error)) {
            plantModel_ = std::move(restored);
            dynamicBranching_.reset();
            growthEvents_.clear();
            keyframes_.clear();
            growthData_.truncateAfter(0.0f);
            nextAutoKeyframeAge_ = 1.0f;
        } else {
            qWarning().noquote() << error;
        }
    }
    const float deltaYears = sample.age - plantModel_.age;
    const std::size_t eventCountBefore = growthEvents_.size();
    if (deltaYears > 0.0f) {
        plantModel_.advanceAge(deltaYears);
        dynamicBranching_.update(plantModel_, sample.age, deltaYears, resourceState(), &growthEvents_);
        for (const GrowthEvent& event : growthEvents_.since(eventCountBefore)) {
            emit growthEventAdded(toString(event.type), event.age, event.nodeId, event.leafId);
        }
    }
    plantModel_.applyGrowthSample(sample);
    environment_.time = sample.age;

    if (physicsEnabled_ || physicsDebugEnabled_) {
        QString physicsError;
        if (!physicsSolver_.synchronizeRestConfiguration(plantModel_, &physicsError)) {
            qWarning().noquote() << QStringLiteral("Plant physics synchronization failed: %1").arg(physicsError);
        } else if (physicsEnabled_) {
            physicsSolver_.step(1.0f / 60.0f, Vec3::Zero(),
                [this, &sample](const Vec3& position, int) {
                    return windField_.sample(position, sample.age, environment_.windIntensity);
                });
            if (!physicsSolver_.applyToPlant(&plantModel_, &physicsError)) {
                qWarning().noquote() << QStringLiteral("Plant physics application failed: %1").arg(physicsError);
            }
        }
        emitPhysicsDebugSnapshot();
    }

    bool topologyChanged = plantModel_.nodeCount() != lastSurfaceNodeCount_;
    for (const GrowthEvent& event : growthEvents_.since(eventCountBefore)) {
        topologyChanged = topologyChanged ||
            event.type == GrowthEvent::Type::BranchCreated ||
            event.type == GrowthEvent::Type::BranchDied;
    }
    const bool physicsChanged = physicsEnabled_ && sample.age != previousAge;
    if (shouldRebuildPlantSurface(sample, topologyChanged, physicsChanged)) {
        rebuildMetaballField();
        rebuildPlantSurface();
        emit plantSurfaceUpdated(plantSurface_);
    }
    while (sample.age >= nextAutoKeyframeAge_) {
        captureGrowthKeyframe(QStringLiteral("auto_%1y").arg(nextAutoKeyframeAge_, 0, 'f', 2));
        nextAutoKeyframeAge_ += 1.0f;
    }
    if (!growthData_.empty() && sample.age + 1.0e-4f < growthData_.frames().back().age) {
        // Continuing from a replayed frame starts a new simulation branch.
        growthData_.truncateAfter(previousAge + 1.0e-4f);
    }
    captureGrowthFrameIfNeeded(topologyChanged);
    emit growthUpdated(buildReport(sample));
}

void SimulationEngine::captureGrowthKeyframe(const QString& label) {
    const GrowthKeyframe& keyframe = keyframes_.capture(plantModel_, plantModel_.age, label, static_cast<int>(growthEvents_.size()));
    growthEvents_.record(plantModel_.age, GrowthEvent::Type::KeyframeCaptured, -1, -1, -1, keyframe.label);
    emit growthKeyframeCaptured(keyframe.id, keyframe.age);
}

bool SimulationEngine::saveGrowthKeyframes(const QString& filePath, QString* error) const {
    return keyframes_.saveJson(filePath, error);
}

bool SimulationEngine::saveGrowthData(const QString& filePath, QString* error) const {
    return growthData_.saveJson(filePath, error);
}

bool SimulationEngine::saveGrowthMetricsCsv(const QString& filePath, QString* error) const {
    return growthData_.saveCsv(filePath, error);
}

QJsonObject SimulationEngine::createPreset() const {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.preset")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("plantProgram"), plantProgram_},
        {QStringLiteral("plant"), plantModel_.toJson()},
        {QStringLiteral("initialPlant"), initialPlantSnapshot_},
        {QStringLiteral("environment"), environment_.toJson()},
        {QStringLiteral("growthTimeline"), growthClock_.timeline().toJson()},
        {QStringLiteral("physics"), QJsonObject{
             {QStringLiteral("enabled"), physicsEnabled_},
             {QStringLiteral("debugEnabled"), physicsDebugEnabled_}}}
    };
}

bool SimulationEngine::applyPreset(const QJsonObject& preset, QString* error) {
    if (preset.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.preset") ||
        preset.value(QStringLiteral("version")).toInt(-1) != 1) {
        setPersistenceError(error, QStringLiteral("Unsupported or missing preset schema."));
        return false;
    }

    PlantModel plant;
    EnvironmentParams environment;
    GrowthTimeline timeline;
    QString validationError;
    if (!PlantModel::fromJson(preset.value(QStringLiteral("plant")).toObject(),
                              &plant, &validationError)) {
        setPersistenceError(error, QStringLiteral("Preset plant is invalid: %1").arg(validationError));
        return false;
    }
    if (!EnvironmentParams::fromJson(preset.value(QStringLiteral("environment")).toObject(),
                                     &environment, &validationError)) {
        setPersistenceError(error, QStringLiteral("Preset environment is invalid: %1").arg(validationError));
        return false;
    }
    if (!GrowthTimeline::fromJson(preset.value(QStringLiteral("growthTimeline")).toObject(),
                                  &timeline, &validationError)) {
        setPersistenceError(error, QStringLiteral("Preset timeline is invalid: %1").arg(validationError));
        return false;
    }
    QJsonObject initialSnapshot = preset.value(QStringLiteral("initialPlant")).toObject();
    if (!initialSnapshot.isEmpty()) {
        PlantModel initialPlant;
        if (!PlantModel::fromJson(initialSnapshot, &initialPlant, &validationError)) {
            setPersistenceError(error, QStringLiteral("Preset initial plant is invalid: %1").arg(validationError));
            return false;
        }
    } else {
        initialSnapshot = plant.toJson();
    }

    restoringRecordedFrame_ = true;
    growthClock_.pause();
    plantModel_ = std::move(plant);
    environment_ = std::move(environment);
    plantProgram_ = preset.value(QStringLiteral("plantProgram")).toString();
    initialPlantSnapshot_ = std::move(initialSnapshot);
    timeline.reset(plantModel_.age);
    timeline.setMode(GrowthPlaybackMode::Paused);
    growthClock_.timeline() = timeline;
    growthClock_.setSpeed(timeline.speed());
    const QJsonObject physics = preset.value(QStringLiteral("physics")).toObject();
    physicsEnabled_ = physics.value(QStringLiteral("enabled")).toBool(false);
    physicsDebugEnabled_ = physics.value(QStringLiteral("debugEnabled")).toBool(false);
    environment_.time = plantModel_.age;
    dynamicBranching_.reset();
    growthEvents_.clear();
    keyframes_.clear();
    growthData_.clear();
    editHistory_.clear();
    nextAutoKeyframeAge_ = std::floor(plantModel_.age) + 1.0f;
    rebuildPhysicsModel();
    rebuildMetaballField();
    rebuildPlantSurface();
    growthData_.capture(plantModel_, true);
    restoringRecordedFrame_ = false;
    ++editRevision_;

    emit environmentUpdated(environment_.lightIntensity);
    emit tropismUpdated(environment_.phototropismWeight, environment_.gravitropismWeight);
    emit plantSurfaceUpdated(plantSurface_);
    emitPhysicsDebugSnapshot();
    emit growthUpdated(buildReport(growthClock_.timeline().sample(plantModel_.age), true));
    emit growthLogMessage(QStringLiteral("Preset loaded at %1y").arg(plantModel_.age, 0, 'f', 2));
    return true;
}

bool SimulationEngine::savePreset(const QString& filePath, QString* error) const {
    return saveJsonObject(filePath, createPreset(), error);
}

bool SimulationEngine::loadPreset(const QString& filePath, QString* error) {
    QJsonObject object;
    return loadJsonObject(filePath, &object, error) && applyPreset(object, error);
}

QJsonObject SimulationEngine::createSceneArchive() const {
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.scene")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("preset"), createPreset()},
        {QStringLiteral("recording"), growthData_.toJson()},
        {QStringLiteral("events"), growthEvents_.toJson()},
        {QStringLiteral("keyframes"), keyframes_.toJson()},
        {QStringLiteral("state"), QJsonObject{
             {QStringLiteral("age"), growthClock_.timeline().currentAge()},
             {QStringLiteral("playbackMode"), toString(growthClock_.timeline().mode())},
             {QStringLiteral("speed"), growthClock_.timeline().speed()},
             {QStringLiteral("editRevision"), static_cast<qint64>(editRevision_)},
             {QStringLiteral("meshVersion"), static_cast<qint64>(meshVersion_)}}}
    };
}

bool SimulationEngine::restoreSceneArchive(const QJsonObject& archive, float restoreAge,
                                           QString* error) {
    if (archive.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.scene") ||
        archive.value(QStringLiteral("version")).toInt(-1) != 1) {
        setPersistenceError(error, QStringLiteral("Unsupported or missing scene archive schema."));
        return false;
    }
    GrowthDataRecorder recording;
    GrowthEventManager events;
    GrowthKeyframeStore keyframes;
    QString validationError;
    if (!GrowthDataRecorder::fromJson(archive.value(QStringLiteral("recording")).toObject(),
                                      &recording, &validationError) ||
        !GrowthEventManager::fromJson(archive.value(QStringLiteral("events")).toArray(),
                                      &events, &validationError) ||
        !GrowthKeyframeStore::fromJson(archive.value(QStringLiteral("keyframes")).toObject(),
                                       &keyframes, &validationError)) {
        setPersistenceError(error, QStringLiteral("Scene history is invalid: %1").arg(validationError));
        return false;
    }
    if (!applyPreset(archive.value(QStringLiteral("preset")).toObject(), error)) return false;

    growthData_ = std::move(recording);
    growthEvents_ = std::move(events);
    keyframes_ = std::move(keyframes);
    const QJsonObject state = archive.value(QStringLiteral("state")).toObject();
    if (restoreAge >= 0.0f && !restoreRecordedScene(restoreAge, error)) return false;

    const float savedSpeed = static_cast<float>(state.value(QStringLiteral("speed")).toDouble(1.0));
    growthClock_.setSpeed(savedSpeed);
    const qint64 archivedEditRevision = static_cast<qint64>(
        state.value(QStringLiteral("editRevision")).toDouble(0.0));
    const qint64 archivedMeshVersion = static_cast<qint64>(
        state.value(QStringLiteral("meshVersion")).toDouble(0.0));
    editRevision_ = static_cast<quint64>(std::max<qint64>(0, archivedEditRevision));
    meshVersion_ = std::max(meshVersion_, static_cast<quint64>(
        std::max<qint64>(0, archivedMeshVersion)));
    if (restoreAge < 0.0f &&
        growthPlaybackModeFromString(state.value(QStringLiteral("playbackMode")).toString()) ==
            GrowthPlaybackMode::Realtime) {
        growthClock_.resume();
    } else {
        emit growthUpdated(buildReport(
            growthClock_.timeline().sample(growthClock_.timeline().currentAge()), true));
    }
    emit growthLogMessage(QStringLiteral("Scene archive restored at %1y")
                              .arg(growthClock_.timeline().currentAge(), 0, 'f', 2));
    return true;
}

bool SimulationEngine::saveSceneArchive(const QString& filePath, QString* error) const {
    return saveJsonObject(filePath, createSceneArchive(), error);
}

bool SimulationEngine::loadSceneArchive(const QString& filePath, float restoreAge,
                                        QString* error) {
    QJsonObject object;
    return loadJsonObject(filePath, &object, error) &&
           restoreSceneArchive(object, restoreAge, error);
}

QJsonObject SimulationEngine::createSceneExportBundle(QString* error) const {
    const SurfaceMesh branches = exportBranchMesh(plantModel_, metaballSettings_, plantSurface_);
    const GeneratedLeaves leaves = generateSceneLeaves(plantModel_);
    const std::vector<std::uint16_t> leafMaterials = globalLeafMaterials(leaves);
    const std::vector<ObjMaterial> materials = sceneMaterials();
    const std::vector<ObjMeshGroup> groups{
        ObjMeshGroup{&branches, QStringLiteral("branches"), 0, nullptr},
        ObjMeshGroup{&leaves.mesh, QStringLiteral("leaves"), 1, &leafMaterials}
    };
    QString obj;
    QString mtl;
    if (!MeshExporter::serializeObj(QStringLiteral("plantsim_scene"), materials, groups,
                                    &obj, &mtl, error)) return {};
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.scene_export")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("objFileName"), QStringLiteral("plantsim_scene.obj")},
        {QStringLiteral("mtlFileName"), QStringLiteral("plantsim_scene.mtl")},
        {QStringLiteral("obj"), obj},
        {QStringLiteral("mtl"), mtl},
        {QStringLiteral("branchTriangles"), static_cast<qint64>(branches.indices.size() / 3)},
        {QStringLiteral("leafTriangles"), static_cast<qint64>(leaves.mesh.indices.size() / 3)}
    };
}

bool SimulationEngine::exportSceneObj(const QString& objPath, QString* error) const {
    const SurfaceMesh branches = exportBranchMesh(plantModel_, metaballSettings_, plantSurface_);
    const GeneratedLeaves leaves = generateSceneLeaves(plantModel_);
    const std::vector<std::uint16_t> leafMaterials = globalLeafMaterials(leaves);
    const std::vector<ObjMaterial> materials = sceneMaterials();
    const std::vector<ObjMeshGroup> groups{
        ObjMeshGroup{&branches, QStringLiteral("branches"), 0, nullptr},
        ObjMeshGroup{&leaves.mesh, QStringLiteral("leaves"), 1, &leafMaterials}
    };
    return MeshExporter::saveObj(objPath, materials, groups, error);
}

void SimulationEngine::captureGrowthFrameIfNeeded(bool forceSnapshot) {
    if (restoringRecordedFrame_) return;
    if (!growthData_.empty()) {
        const GrowthDataFrame* latest = growthData_.at(growthData_.size() - 1);
        if (latest && std::abs(latest->age - plantModel_.age) < 1.0e-4f) return;
    }
    growthData_.capture(plantModel_, forceSnapshot);
}

void SimulationEngine::onGrowthClockLog(const QString& message) {
    qInfo().noquote() << QStringLiteral("[Growth] %1").arg(message);
    emit growthLogMessage(message);
}

bool SimulationEngine::applyScaleEdit(int nodeId, const ScaleParams& params,
                                      bool preview, QString* error) {
    const bool implicitTransaction = !editHistory_.hasActiveEdit();
    if (implicitTransaction) beginEdit();
    if (!ScaleTool::apply(plantModel_, nodeId, params, error) ||
        !finalizeEdit(EditDirtyFlag::TransformChanged, preview, error)) {
        // Do not call cancelEdit() here: a rejected implicit command must
        // restore its snapshot without treating the rollback as a user-visible
        // edit (and therefore without rebuilding the surface or advancing its
        // revision). The tools validate before mutation in normal cases, while
        // this still safely rolls back a partially-applied validation failure.
        if (implicitTransaction) {
            QString rollbackError;
            editHistory_.cancel(&plantModel_, &rollbackError);
        }
        return false;
    }
    if (implicitTransaction && !preview) commitEditInternal(false);
    return true;
}

bool SimulationEngine::applyBendEdit(int nodeId, const BendParams& params,
                                     bool preview, QString* error) {
    const bool implicitTransaction = !editHistory_.hasActiveEdit();
    if (implicitTransaction) beginEdit();
    if (!BendTool::apply(plantModel_, nodeId, params, error) ||
        !finalizeEdit(EditDirtyFlag::TransformChanged, preview, error)) {
        // Do not call cancelEdit() here: a rejected implicit command must
        // restore its snapshot without treating the rollback as a user-visible
        // edit (and therefore without rebuilding the surface or advancing its
        // revision). The tools validate before mutation in normal cases, while
        // this still safely rolls back a partially-applied validation failure.
        if (implicitTransaction) {
            QString rollbackError;
            editHistory_.cancel(&plantModel_, &rollbackError);
        }
        return false;
    }
    if (implicitTransaction && !preview) commitEditInternal(false);
    return true;
}

bool SimulationEngine::applyRotateEdit(int nodeId, const Vec3& pivot, const Vec3& axis,
                                       float angleRadians, bool preview, QString* error) {
    const bool implicitTransaction = !editHistory_.hasActiveEdit();
    if (implicitTransaction) beginEdit();
    if (!RotateTool::apply(plantModel_, nodeId, pivot, axis, angleRadians, error) ||
        !finalizeEdit(EditDirtyFlag::TransformChanged, preview, error)) {
        // Do not call cancelEdit() here: a rejected implicit command must
        // restore its snapshot without treating the rollback as a user-visible
        // edit (and therefore without rebuilding the surface or advancing its
        // revision). The tools validate before mutation in normal cases, while
        // this still safely rolls back a partially-applied validation failure.
        if (implicitTransaction) {
            QString rollbackError;
            editHistory_.cancel(&plantModel_, &rollbackError);
        }
        return false;
    }
    if (implicitTransaction && !preview) commitEditInternal(false);
    return true;
}

bool SimulationEngine::applyNodeParameterEdit(int nodeId, const NodeParameterUpdate& update,
                                              bool preview, QString* error) {
    const bool implicitTransaction = !editHistory_.hasActiveEdit();
    if (implicitTransaction) beginEdit();
    if (!NodeParameterEditor::apply(plantModel_, nodeId, update, error) ||
        !finalizeEdit(EditDirtyFlag::ParameterChanged, preview, error)) {
        // Do not call cancelEdit() here: a rejected implicit command must
        // restore its snapshot without treating the rollback as a user-visible
        // edit (and therefore without rebuilding the surface or advancing its
        // revision). The tools validate before mutation in normal cases, while
        // this still safely rolls back a partially-applied validation failure.
        if (implicitTransaction) {
            QString rollbackError;
            editHistory_.cancel(&plantModel_, &rollbackError);
        }
        return false;
    }
    if (implicitTransaction && !preview) commitEditInternal(false);
    return true;
}

void SimulationEngine::beginEdit() {
    editHistory_.begin(plantModel_);
}

bool SimulationEngine::commitEdit() {
    return commitEditInternal(true);
}

bool SimulationEngine::commitEditInternal(bool rebuildCommittedMesh, QString* error) {
    const bool changed = editHistory_.commit(plantModel_);
    if (!changed || !rebuildCommittedMesh) return changed;
    return restoreAfterEditHistoryChange(error);
}

bool SimulationEngine::cancelEdit(QString* error) {
    if (!editHistory_.cancel(&plantModel_, error)) return false;
    return restoreAfterEditHistoryChange(error);
}

bool SimulationEngine::undoLastEdit(QString* error) {
    if (!editHistory_.undo(&plantModel_, error)) return false;
    return restoreAfterEditHistoryChange(error);
}

bool SimulationEngine::canUndo() const {
    return editHistory_.canUndo();
}

void SimulationEngine::resetPlant() {
    resetGrowth(0.0f);
    editHistory_.clear();
    ++editRevision_;
    emit growthUpdated(buildReport(growthClock_.timeline().sample(plantModel_.age), true));
}

bool SimulationEngine::restoreAfterEditHistoryChange(QString* error) {
    return finalizeEdit(EditDirtyFlag::ParameterChanged, false, error);
}

void SimulationEngine::rebuildPlantMesh() {
    rebuildMetaballField();
    rebuildPlantSurface();
    emit plantSurfaceUpdated(plantSurface_);
}

bool SimulationEngine::finalizeEdit(EditDirtyFlag dirty, bool preview, QString* error) {
    if (dirty == EditDirtyFlag::None) return true;
    if (!plantModel_.validate(error)) return false;

    // The edited pose becomes the baseline for subsequent analytical growth.
    // Rebase rather than merely recapturing values so the current timeline
    // scale is not multiplied into length/radius/leaf size twice next tick.
    const GrowthSample sample = growthClock_.timeline().sample(plantModel_.age);
    plantModel_.rebaseGrowthBaselines(sample);
    rebuildPhysicsModel();
    rebuildMetaballField();
    rebuildPlantSurface(preview ? 0.26f : 0.18f);
    ++editRevision_;
    emit plantSurfaceUpdated(plantSurface_);
    emit growthUpdated(buildReport(sample, true));
    if (physicsDebugEnabled_) emitPhysicsDebugSnapshot();
    return true;
}

void SimulationEngine::rebuildPhysicsModel() {
    QString error;
    if (!physicsSolver_.rebuildFromPlant(plantModel_, &error)) {
        qWarning().noquote() << QStringLiteral("Plant physics rebuild failed: %1").arg(error);
    }
}

void SimulationEngine::emitPhysicsDebugSnapshot() {
    if (physicsDebugEnabled_) emit physicsDebugUpdated(physicsSolver_.debugSnapshot());
}

void SimulationEngine::rebuildMetaballField() {
    metaballField_.rebuildFromPlant(plantModel_, metaballSettings_);
}

void SimulationEngine::rebuildPlantSurface(float requestedSpacing) {
    const float spacing = adaptiveSurfaceSpacing(requestedSpacing);
    const ScalarFieldGrid grid = metaballField_.sampleGrid(spacing, 100000);
    plantSurface_ = MarchingCubes::extract(grid, metaballField_.isoThreshold());
    ++meshVersion_;
    rememberSurfaceState(growthClock_.timeline().sample(plantModel_.age));
}

bool SimulationEngine::shouldRebuildPlantSurface(const GrowthSample& sample,
                                                 bool topologyChanged,
                                                 bool physicsChanged) const {
    if (!surfaceStateInitialized_ || topologyChanged || physicsChanged) {
        return true;
    }

    const float speed = std::max(0.1f, growthClock_.timeline().speed());
    const float scaleDelta = std::max({std::abs(sample.lengthScale - lastSurfaceLengthScale_),
                                       std::abs(sample.radiusScale - lastSurfaceRadiusScale_),
                                       (sample.leafScale - lastSurfaceLeafScale_).cwiseAbs().maxCoeff()});
    // Coalesce small analytical growth changes. At higher playback speeds, keep
    // the newest geometry while accepting a larger visual age interval.
    const float minimumAgeStep = 0.075f * std::sqrt(speed);
    return scaleDelta >= 0.0125f ||
           sample.age - lastSurfaceAge_ >= minimumAgeStep;
}

float SimulationEngine::adaptiveSurfaceSpacing(float requestedSpacing) const {
    const float speed = std::max(0.1f, growthClock_.timeline().speed());
    const float speedFactor = std::clamp(std::sqrt(speed), 1.0f, 1.8f);
    return std::max(0.0001f, requestedSpacing * speedFactor);
}

void SimulationEngine::rememberSurfaceState(const GrowthSample& sample) {
    surfaceStateInitialized_ = true;
    lastSurfaceAge_ = sample.age;
    lastSurfaceLengthScale_ = sample.lengthScale;
    lastSurfaceRadiusScale_ = sample.radiusScale;
    lastSurfaceLeafScale_ = sample.leafScale;
    lastSurfaceNodeCount_ = plantModel_.nodeCount();
}

void SimulationEngine::collectAllNodePositions(const PlantNode* node, std::vector<Vec3>* positions) const {
    if (!node || !positions) return;
    positions->push_back(node->position);
    for (const auto& child : node->children) {
        collectAllNodePositions(child.get(), positions);
    }
}

GrowthResourceState SimulationEngine::resourceState() const {
    GrowthResourceState state;
    state.light = environment_.lightIntensity;
    state.moisture = environment_.moisture;
    state.nutrition = environment_.nutrition;
    state.temperature = environment_.temperature;
    state.wind = environment_.windIntensity;
    state.environment = environment_;
    collectAllNodePositions(plantModel_.rootNode(), &state.allNodePositions);
    return state;
}

GrowthStateReport SimulationEngine::buildReport(const GrowthSample& sample,
                                                bool includePlantState) const {
    GrowthStateReport report;
    report.age         = sample.age;
    report.lifeStage   = sample.lifeStage;
    report.growthState = plantModel_.growthState;
    report.lengthScale = sample.lengthScale;
    report.radiusScale = sample.radiusScale;
    report.leafScale   = sample.leafScale;
    report.speed       = growthClock_.timeline().speed();
    report.mode        = static_cast<int>(growthClock_.timeline().mode());
    report.nodeCount   = static_cast<int>(plantModel_.nodeCount());
    report.branchCount = static_cast<int>(plantModel_.branches().size());
    report.leafCount   = static_cast<int>(plantModel_.leaves().size());
    if (const PlantGrowthMetrics* cached = growthData_.cachedMetrics(plantModel_.age)) {
        report.metrics = *cached;
    } else {
        report.metrics = GrowthDataRecorder::measure(plantModel_);
    }
    report.recordedFrameCount = static_cast<int>(growthData_.size());
    report.recordedEndAge = growthData_.empty() ? 0.0f : growthData_.frames().back().age;
    if (includePlantState) {
        report.plantState = plantModel_.toJson();
    }
    return report;
}
