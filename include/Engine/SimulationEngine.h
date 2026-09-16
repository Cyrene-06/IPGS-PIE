// ============================================================================
// SimulationEngine - environment, plant state bridge, and growth time controller
// 第11周：向光性/向地性参数与多光源状态同步
// ============================================================================
#pragma once

#include <cstddef>

#include <QObject>
#include <QJsonObject>

#include "Algorithm/LSystem.h"
#include "Engine/EnvironmentParams.h"
#include "Engine/GrowthClock.h"
#include "Engine/GrowthStateReport.h"
#include "Engine/GrowthDataRecorder.h"
#include "Engine/DynamicBranchingSystem.h"
#include "Engine/GrowthEventManager.h"
#include "Engine/GrowthKeyframeStore.h"
#include "Engine/GrowthTimeline.h"
#include "Engine/WindField.h"
#include "Editing/BendTool.h"
#include "Editing/EditTypes.h"
#include "Editing/EditHistory.h"
#include "Editing/NodeParameterEditor.h"
#include "Editing/RotateTool.h"
#include "Editing/ScaleTool.h"
#include "Geometry/MarchingCubes.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"
#include "Physics/PlantPhysicsSolver.h"

class SimulationEngine : public QObject {
    Q_OBJECT
public:
    explicit SimulationEngine(QObject* parent = nullptr);

    const EnvironmentParams& environment() const;
    EnvironmentParams& mutableEnvironment();
    const PlantModel& plantModel() const;
    const PlantNode* plantRoot() const;
    const QString& plantProgram() const;
    std::size_t plantNodeCount() const;
    const MetaballField& metaballField() const;

    // 生长时间轴访问器
    const GrowthTimeline& growthTimeline() const { return growthClock_.timeline(); }
    GrowthTimeline&       growthTimeline()       { return growthClock_.timeline(); }
    const GrowthClock&    growthClock()    const { return growthClock_; }
    GrowthClock&          growthClock()          { return growthClock_; }

    // 植物表面网格与状态访问器
    const SurfaceMesh& plantSurface() const { return plantSurface_; }
    const GrowthEventManager& growthEvents() const { return growthEvents_; }
    const GrowthKeyframeStore& growthKeyframes() const { return keyframes_; }
    const GrowthDataRecorder& growthData() const { return growthData_; }
    const PlantPhysicsSolver& physicsSolver() const { return physicsSolver_; }
    bool physicsEnabled() const { return physicsEnabled_; }
    bool physicsDebugEnabled() const { return physicsDebugEnabled_; }

    // Interactive-edit bridge. Every accepted edit synchronizes the PBD model,
    // metaball field and Marching Cubes mesh before notifying render clients.
    bool applyScaleEdit(int nodeId, const ScaleParams& params, bool preview, QString* error = nullptr);
    bool applyBendEdit(int nodeId, const BendParams& params, bool preview, QString* error = nullptr);
    bool applyRotateEdit(int nodeId, const Vec3& pivot, const Vec3& axis,
                         float angleRadians, bool preview, QString* error = nullptr);
    bool applyNodeParameterEdit(int nodeId, const NodeParameterUpdate& update,
                                bool preview, QString* error = nullptr);
    void rebuildPlantMesh();
    quint64 editRevision() const { return editRevision_; }
    quint64 meshVersion() const { return meshVersion_; }

    // A single gesture stores one pre-edit snapshot irrespective of how many
    // preview updates it produces. Undo and reset both rebuild every runtime
    // representation before notifying observers.
    void beginEdit();
    bool commitEdit();
    bool cancelEdit(QString* error = nullptr);
    bool undoLastEdit(QString* error = nullptr);
    bool canUndo() const;
    void resetPlant();

public slots:
    void setLightIntensity(float intensity);
    void setPhototropismWeight(float weight);
    void setGravitropismWeight(float weight);
    void setLightSourcePosition(int lightId, float x, float y, float z);
    void setPhysicsEnabled(bool enabled);
    void setPhysicsDebugEnabled(bool enabled);

    // 生长时间轴控制
    void startGrowth();
    void pauseGrowth();
    void resumeGrowth();
    void resetGrowth(float initialYears = 0.0f);
    void setGrowthSpeed(float speed);                 // 0.1 .. 8.0
    void stepGrowth(float deltaYears);
    // Restores the nearest recorded full-state checkpoint and leaves playback paused.
    void seekGrowth(float age);
    void jumpToGrowthStage(const QString& stage);
    void requestGrowthData();
    void captureGrowthKeyframe(const QString& label = QString());
    bool saveGrowthKeyframes(const QString& filePath, QString* error = nullptr) const;
    bool saveGrowthData(const QString& filePath, QString* error = nullptr) const;
    bool saveGrowthMetricsCsv(const QString& filePath, QString* error = nullptr) const;

    // Week 16 persistence: presets retain plant/environment parameters, while
    // scene archives additionally retain replay history and playback state.
    QJsonObject createPreset() const;
    bool applyPreset(const QJsonObject& preset, QString* error = nullptr);
    bool savePreset(const QString& filePath, QString* error = nullptr) const;
    bool loadPreset(const QString& filePath, QString* error = nullptr);
    QJsonObject createSceneArchive() const;
    bool restoreSceneArchive(const QJsonObject& archive, float restoreAge = -1.0f,
                             QString* error = nullptr);
    bool saveSceneArchive(const QString& filePath, QString* error = nullptr) const;
    bool loadSceneArchive(const QString& filePath, float restoreAge = -1.0f,
                          QString* error = nullptr);
    bool restoreRecordedScene(float age, QString* error = nullptr);
    bool exportSceneObj(const QString& objPath, QString* error = nullptr) const;
    QJsonObject createSceneExportBundle(QString* error = nullptr) const;

signals:
    void environmentUpdated(float lightIntensity);
    void tropismUpdated(float photoWeight, float graviWeight);
    void logMessage(const QString& message);

    // 生长状态广播
    void growthUpdated(const GrowthStateReport& report);
    void growthLogMessage(const QString& message);
    void growthEventAdded(const QString& type, float age, int nodeId, int leafId);
    void growthKeyframeCaptured(int keyframeId, float age);
    void growthDataAvailable(const QJsonObject& data);
    void plantSurfaceUpdated(const SurfaceMesh& mesh);
    void physicsDebugUpdated(const PlantPhysicsDebugSnapshot& snapshot);

private slots:
    void onGrowthTickProduced(const GrowthSample& sample);
    void onGrowthClockLog(const QString& message);

private:
    void rebuildMetaballField();
    void rebuildPlantSurface(float requestedSpacing = 0.18f);
    bool shouldRebuildPlantSurface(const GrowthSample& sample,
                                   bool topologyChanged,
                                   bool physicsChanged) const;
    float adaptiveSurfaceSpacing(float requestedSpacing) const;
    void rememberSurfaceState(const GrowthSample& sample);
    GrowthStateReport buildReport(const GrowthSample& sample,
                                  bool includePlantState = false) const;
    void captureGrowthFrameIfNeeded(bool forceSnapshot = false);
    GrowthResourceState resourceState() const;
    void collectAllNodePositions(const PlantNode* node, std::vector<Vec3>* positions) const;
    void rebuildPhysicsModel();
    void emitPhysicsDebugSnapshot();
    bool finalizeEdit(EditDirtyFlag dirty, bool preview, QString* error);
    bool restoreAfterEditHistoryChange(QString* error);
    bool commitEditInternal(bool rebuildCommittedMesh, QString* error = nullptr);

    EnvironmentParams environment_;
    LSystem lSystem_;
    QString plantProgram_;
    PlantModel plantModel_;
    MetaballField metaballField_;
    MetaballFieldSettings metaballSettings_;
    SurfaceMesh plantSurface_;
    GrowthClock growthClock_;
    DynamicBranchingSystem dynamicBranching_;
    GrowthEventManager growthEvents_;
    GrowthKeyframeStore keyframes_;
    GrowthDataRecorder growthData_;
    EditHistory editHistory_;
    PerlinWindField windField_;
    PlantPhysicsSolver physicsSolver_;
    bool physicsEnabled_ = false;
    bool physicsDebugEnabled_ = false;
    bool restoringRecordedFrame_ = false;
    bool surfaceStateInitialized_ = false;
    QJsonObject initialPlantSnapshot_;
    float lastSurfaceAge_ = 0.0f;
    float lastSurfaceLengthScale_ = 0.0f;
    float lastSurfaceRadiusScale_ = 0.0f;
    Vec2 lastSurfaceLeafScale_ = Vec2::Zero();
    std::size_t lastSurfaceNodeCount_ = 0;
    float nextAutoKeyframeAge_ = 1.0f;
    quint64 editRevision_ = 0;
    quint64 meshVersion_ = 0;
};
