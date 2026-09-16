#pragma once

#include <cstddef>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Plant/PlantModel.h"

// A compact set of plant indicators collected for every simulation time step.
struct PlantGrowthMetrics {
    float height = 0.0f;
    float totalBranchLength = 0.0f;
    int branchCount = 0;
    int leafCount = 0;
    float canopyWidth = 0.0f;
};

// A metric frame is recorded for every simulation time step. plantState is
// populated only for periodic/forced replay checkpoints, which substantially
// reduces long-running recording memory use.
struct GrowthDataFrame {
    int step = 0;
    float age = 0.0f;
    PlantLifeStage lifeStage = PlantLifeStage::Seedling;
    PlantGrowthMetrics metrics;
    QJsonObject plantState;

    bool hasPlantState() const { return !plantState.isEmpty(); }
};

class GrowthDataRecorder {
public:
    static constexpr std::size_t kDefaultSnapshotInterval = 30;
    const std::vector<GrowthDataFrame>& frames() const { return frames_; }
    std::size_t size() const { return frames_.size(); }
    bool empty() const { return frames_.empty(); }

    void clear();
    void truncateAfter(float age);
    void setSnapshotInterval(std::size_t frameInterval);
    std::size_t snapshotInterval() const { return snapshotInterval_; }
    std::size_t snapshotCount() const { return snapshotIndices_.size(); }

    // Records metrics on every call. A lossless state is retained for the
    // first frame, every snapshotInterval() frames, and forced checkpoints.
    const GrowthDataFrame& capture(const PlantModel& plant, bool forceSnapshot = false);
    const GrowthDataFrame* nearest(float age) const;
    const GrowthDataFrame* nearestSnapshot(float age) const;
    const GrowthDataFrame* at(std::size_t index) const;
    // Returns the cached last-frame metrics only when they match this age.
    // This lets the engine build a tick report without measuring the tree twice.
    const PlantGrowthMetrics* cachedMetrics(float age, float tolerance = 1.0e-4f) const;

    static PlantGrowthMetrics measure(const PlantModel& plant);

    // Archive retains every metric frame and periodic lossless checkpoints.
    QJsonObject toJson() const;
    // Lightweight archive is intended for the browser chart and downloads.
    QJsonObject metricsToJson() const;
    static bool fromJson(const QJsonObject& json, GrowthDataRecorder* output,
                         QString* error = nullptr);
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    static bool loadJson(const QString& filePath, GrowthDataRecorder* output,
                         QString* error = nullptr);
    bool saveCsv(const QString& filePath, QString* error = nullptr) const;

private:
    void rebuildSnapshotIndex();

    std::vector<GrowthDataFrame> frames_;
    std::vector<std::size_t> snapshotIndices_;
    std::size_t snapshotInterval_ = kDefaultSnapshotInterval;
};
