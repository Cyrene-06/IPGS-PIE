#include "Engine/GrowthDataRecorder.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

namespace {

void setError(QString* error, const QString& value) {
    if (error) *error = value;
}

QJsonObject metricsJson(const PlantGrowthMetrics& metrics) {
    return QJsonObject{
        {"height", static_cast<double>(metrics.height)},
        {"totalBranchLength", static_cast<double>(metrics.totalBranchLength)},
        {"branchCount", metrics.branchCount},
        {"leafCount", metrics.leafCount},
        {"canopyWidth", static_cast<double>(metrics.canopyWidth)}
    };
}

QJsonObject frameJson(const GrowthDataFrame& frame, bool includePlantState) {
    QJsonObject result{
        {"step", frame.step},
        {"age", static_cast<double>(frame.age)},
        {"lifeStage", toString(frame.lifeStage)},
        {"metrics", metricsJson(frame.metrics)}
    };
    if (includePlantState && frame.hasPlantState()) result.insert("plantState", frame.plantState);
    return result;
}

void includePoint(const Vec3& point, float padding,
                  float* minY, float* maxY,
                  float* minX, float* maxX,
                  float* minZ, float* maxZ) {
    *minY = std::min(*minY, point.y() - padding);
    *maxY = std::max(*maxY, point.y() + padding);
    *minX = std::min(*minX, point.x() - padding);
    *maxX = std::max(*maxX, point.x() + padding);
    *minZ = std::min(*minZ, point.z() - padding);
    *maxZ = std::max(*maxZ, point.z() + padding);
}

void measureNodes(const PlantNode* node,
                  PlantGrowthMetrics* metrics,
                  float* minY, float* maxY,
                  float* minX, float* maxX,
                  float* minZ, float* maxZ) {
    if (!node) return;
    includePoint(node->position, std::max(0.0f, node->radius), minY, maxY, minX, maxX, minZ, maxZ);
    for (const auto& child : node->children) {
        if (child->active) {
            ++metrics->branchCount;
            const float geometricLength = (child->position - node->position).norm();
            metrics->totalBranchLength += geometricLength > 1.0e-5f ? geometricLength : std::max(0.0f, child->length);
        }
        measureNodes(child.get(), metrics, minY, maxY, minX, maxX, minZ, maxZ);
    }
}

}  // namespace

void GrowthDataRecorder::clear() {
    frames_.clear();
    snapshotIndices_.clear();
}

void GrowthDataRecorder::rebuildSnapshotIndex() {
    snapshotIndices_.clear();
    for (std::size_t index = 0; index < frames_.size(); ++index) {
        if (frames_[index].hasPlantState()) snapshotIndices_.push_back(index);
    }
}

void GrowthDataRecorder::truncateAfter(float age) {
    const auto firstFuture = std::upper_bound(frames_.begin(), frames_.end(), age,
        [](float value, const GrowthDataFrame& frame) { return value < frame.age; });
    frames_.erase(firstFuture, frames_.end());
    rebuildSnapshotIndex();
}

void GrowthDataRecorder::setSnapshotInterval(std::size_t frameInterval) {
    snapshotInterval_ = std::max<std::size_t>(1, frameInterval);
}

const GrowthDataFrame& GrowthDataRecorder::capture(const PlantModel& plant, bool forceSnapshot) {
    GrowthDataFrame frame;
    frame.step = static_cast<int>(frames_.size());
    frame.age = plant.age;
    frame.lifeStage = plant.lifeStage;
    frame.metrics = measure(plant);
    const bool storeSnapshot = forceSnapshot || frames_.empty() ||
                               (frames_.size() % snapshotInterval_ == 0);
    if (storeSnapshot) frame.plantState = plant.toJson();
    frames_.push_back(std::move(frame));
    if (storeSnapshot) snapshotIndices_.push_back(frames_.size() - 1);
    return frames_.back();
}

const GrowthDataFrame* GrowthDataRecorder::nearest(float age) const {
    if (frames_.empty()) return nullptr;
    const auto it = std::lower_bound(frames_.begin(), frames_.end(), age,
        [](const GrowthDataFrame& frame, float value) { return frame.age < value; });
    if (it == frames_.begin()) return &(*it);
    if (it == frames_.end()) return &frames_.back();
    const GrowthDataFrame& after = *it;
    const GrowthDataFrame& before = *(it - 1);
    return std::abs(after.age - age) < std::abs(age - before.age) ? &after : &before;
}

const GrowthDataFrame* GrowthDataRecorder::nearestSnapshot(float age) const {
    if (snapshotIndices_.empty()) return nullptr;
    const auto it = std::lower_bound(snapshotIndices_.begin(), snapshotIndices_.end(), age,
        [this](std::size_t index, float value) { return frames_[index].age < value; });
    if (it == snapshotIndices_.begin()) return &frames_[*it];
    if (it == snapshotIndices_.end()) return &frames_[snapshotIndices_.back()];
    const GrowthDataFrame& after = frames_[*it];
    const GrowthDataFrame& before = frames_[*(it - 1)];
    return std::abs(after.age - age) < std::abs(age - before.age) ? &after : &before;
}

const GrowthDataFrame* GrowthDataRecorder::at(std::size_t index) const {
    return index < frames_.size() ? &frames_[index] : nullptr;
}

const PlantGrowthMetrics* GrowthDataRecorder::cachedMetrics(float age, float tolerance) const {
    if (frames_.empty() || std::abs(frames_.back().age - age) > std::max(0.0f, tolerance)) return nullptr;
    return &frames_.back().metrics;
}

PlantGrowthMetrics GrowthDataRecorder::measure(const PlantModel& plant) {
    PlantGrowthMetrics metrics;
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    measureNodes(plant.rootNode(), &metrics, &minY, &maxY, &minX, &maxX, &minZ, &maxZ);
    for (const Leaf& leaf : plant.leaves()) {
        if (!leaf.active) continue;
        ++metrics.leafCount;
        const float spread = std::max(0.0f, std::max(leaf.size.x(), leaf.size.y()) * 0.5f);
        includePoint(leaf.position, spread, &minY, &maxY, &minX, &maxX, &minZ, &maxZ);
    }

    if (minY != std::numeric_limits<float>::max()) {
        metrics.height = std::max(0.0f, maxY - minY);
        metrics.canopyWidth = std::max(maxX - minX, maxZ - minZ);
    }
    return metrics;
}

QJsonObject GrowthDataRecorder::toJson() const {
    QJsonArray frames;
    for (const GrowthDataFrame& frame : frames_) frames.append(frameJson(frame, true));
    return QJsonObject{
        {"schema", "plantsim.growth_recording"},
        {"version", 2},
        {"frameCount", static_cast<int>(frames_.size())},
        {"snapshotFrameCount", static_cast<int>(snapshotIndices_.size())},
        {"snapshotIntervalFrames", static_cast<int>(snapshotInterval_)},
        {"frames", frames}
    };
}

QJsonObject GrowthDataRecorder::metricsToJson() const {
    QJsonArray frames;
    for (const GrowthDataFrame& frame : frames_) frames.append(frameJson(frame, false));
    return QJsonObject{
        {"schema", "plantsim.growth_metrics"},
        {"version", 2},
        {"frameCount", static_cast<int>(frames_.size())},
        {"snapshotFrameCount", static_cast<int>(snapshotIndices_.size())},
        {"snapshotIntervalFrames", static_cast<int>(snapshotInterval_)},
        {"frames", frames}
    };
}

bool GrowthDataRecorder::fromJson(const QJsonObject& json, GrowthDataRecorder* output,
                                  QString* error) {
    if (!output) {
        setError(error, QStringLiteral("Output GrowthDataRecorder pointer is null."));
        return false;
    }
    if (json.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.growth_recording") ||
        json.value(QStringLiteral("version")).toInt(-1) != 2) {
        setError(error, QStringLiteral("Unsupported or missing growth recording schema."));
        return false;
    }
    const QJsonValue framesValue = json.value(QStringLiteral("frames"));
    if (!framesValue.isArray()) {
        setError(error, QStringLiteral("Growth recording frames must be an array."));
        return false;
    }

    GrowthDataRecorder parsed;
    const int snapshotInterval = json.value(QStringLiteral("snapshotIntervalFrames")).toInt(
        static_cast<int>(kDefaultSnapshotInterval));
    if (snapshotInterval <= 0) {
        setError(error, QStringLiteral("snapshotIntervalFrames must be positive."));
        return false;
    }
    parsed.snapshotInterval_ = static_cast<std::size_t>(snapshotInterval);
    float previousAge = -1.0f;
    int expectedStep = 0;
    for (const QJsonValue& value : framesValue.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("Every growth frame must be an object."));
            return false;
        }
        const QJsonObject object = value.toObject();
        const QJsonObject metrics = object.value(QStringLiteral("metrics")).toObject();
        GrowthDataFrame frame;
        frame.step = object.value(QStringLiteral("step")).toInt(-1);
        frame.age = static_cast<float>(object.value(QStringLiteral("age")).toDouble(-1.0));
        frame.lifeStage = plantLifeStageFromString(object.value(QStringLiteral("lifeStage")).toString());
        frame.metrics.height = static_cast<float>(metrics.value(QStringLiteral("height")).toDouble(-1.0));
        frame.metrics.totalBranchLength = static_cast<float>(metrics.value(QStringLiteral("totalBranchLength")).toDouble(-1.0));
        frame.metrics.branchCount = metrics.value(QStringLiteral("branchCount")).toInt(-1);
        frame.metrics.leafCount = metrics.value(QStringLiteral("leafCount")).toInt(-1);
        frame.metrics.canopyWidth = static_cast<float>(metrics.value(QStringLiteral("canopyWidth")).toDouble(-1.0));
        if (frame.step != expectedStep || !std::isfinite(frame.age) || frame.age < previousAge ||
            !std::isfinite(frame.metrics.height) || frame.metrics.height < 0.0f ||
            !std::isfinite(frame.metrics.totalBranchLength) || frame.metrics.totalBranchLength < 0.0f ||
            frame.metrics.branchCount < 0 || frame.metrics.leafCount < 0 ||
            !std::isfinite(frame.metrics.canopyWidth) || frame.metrics.canopyWidth < 0.0f) {
            setError(error, QStringLiteral("Growth frame %1 contains invalid or unordered data.").arg(expectedStep));
            return false;
        }
        if (object.value(QStringLiteral("plantState")).isObject()) {
            frame.plantState = object.value(QStringLiteral("plantState")).toObject();
            PlantModel validated;
            QString validationError;
            if (!PlantModel::fromJson(frame.plantState, &validated, &validationError)) {
                setError(error, QStringLiteral("Growth frame %1 has an invalid plant state: %2")
                                   .arg(expectedStep).arg(validationError));
                return false;
            }
        }
        parsed.frames_.push_back(std::move(frame));
        previousAge = parsed.frames_.back().age;
        ++expectedStep;
    }
    parsed.rebuildSnapshotIndex();
    if (!parsed.frames_.empty() && parsed.snapshotIndices_.empty()) {
        setError(error, QStringLiteral("A non-empty growth recording requires at least one plant snapshot."));
        return false;
    }
    *output = std::move(parsed);
    return true;
}

bool GrowthDataRecorder::saveJson(const QString& filePath, QString* error) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot write growth recording: %1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool GrowthDataRecorder::loadJson(const QString& filePath, GrowthDataRecorder* output,
                                  QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open growth recording: %1").arg(file.errorString()));
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid growth recording JSON: %1").arg(parseError.errorString()));
        return false;
    }
    return fromJson(document.object(), output, error);
}

bool GrowthDataRecorder::saveCsv(const QString& filePath, QString* error) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(error, QStringLiteral("Cannot write growth CSV: %1").arg(file.errorString()));
        return false;
    }
    QTextStream stream(&file);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);
    stream << "step,age_years,life_stage,height,total_branch_length,branch_count,leaf_count,canopy_width\n";
    for (const GrowthDataFrame& frame : frames_) {
        const PlantGrowthMetrics& metrics = frame.metrics;
        stream << frame.step << ',' << frame.age << ',' << toString(frame.lifeStage) << ','
               << metrics.height << ',' << metrics.totalBranchLength << ','
               << metrics.branchCount << ',' << metrics.leafCount << ','
               << metrics.canopyWidth << '\n';
    }
    return true;
}
