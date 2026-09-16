// ============================================================================
// EnvironmentParams - shared environment state for the simulation and renderer
// 第11周：扩展多光源支持、光照遮挡判断与向性参数
// ============================================================================
#pragma once

#include <vector>
#include <QJsonObject>
#include <QString>
#include "Common/MathTypes.h"

enum class LightType {
    Directional,
    Point
};

struct LightSource {
    int id = 0;
    LightType type = LightType::Directional;
    Vec3 position = Vec3(2.0f, 4.0f, 2.0f);
    Vec3 direction = Vec3(-0.35f, -0.85f, -0.45f).normalized();
    Vec3 color = Vec3(1.0f, 0.95f, 0.82f);
    float intensity = 0.8f;
    bool enabled = true;
};

struct EnvironmentParams {
    Vec3 lightDirection = Vec3(-0.35f, 0.85f, 0.45f).normalized();
    float lightIntensity = 0.8f;
    float moisture = 0.72f;
    float nutrition = 0.64f;
    float temperature = 22.0f;
    float windIntensity = 0.28f;
    float time = 0.0f;

    // 第11周：向光性与向地性控制参数
    float phototropismWeight = 0.45f;  // 茎枝朝向有效光线的向光性权重 (0.0 ~ 1.0)
    float gravitropismWeight = 0.35f;  // 茎向上/根向下的向地性权重 (0.0 ~ 1.0)
    float occlusionRadius = 0.22f;     // 简化阴影遮挡判定半径
    float shadowFactor = 0.40f;        // 受遮挡时的光照衰减系数 (0.0 ~ 1.0)

    // 多光源列表
    std::vector<LightSource> lightSources;

    EnvironmentParams();

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, EnvironmentParams* output,
                         QString* error = nullptr);

    // 计算点 position 处的加权有效光照方向矢量和总受光强度
    Vec3 calculateEffectiveLightDirection(const Vec3& position, float* outTotalIntensity = nullptr) const;

    // 计算点 position 处在 nodePositions 节点集合遮挡下的受光百分比 (0.0 ~ 1.0)
    float calculateLightExposure(const Vec3& position, const std::vector<Vec3>& occluderPositions) const;
};
