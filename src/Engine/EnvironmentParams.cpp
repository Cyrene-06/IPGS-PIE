// ============================================================================
// EnvironmentParams implementation
// 第11周：多光源合成矢量与简化光照遮挡算法
// ============================================================================
#include "Engine/EnvironmentParams.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>

namespace {
constexpr float kEpsilon = 1.0e-5f;

QJsonArray vectorJson(const Vec3& value) {
    return QJsonArray{value.x(), value.y(), value.z()};
}

bool readFinite(const QJsonValue& value, float* output) {
    if (!output || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number)) return false;
    *output = static_cast<float>(number);
    return std::isfinite(*output);
}

bool readVector(const QJsonValue& value, Vec3* output) {
    if (!output || !value.isArray()) return false;
    const QJsonArray values = value.toArray();
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (values.size() != 3 || !readFinite(values.at(0), &x) ||
        !readFinite(values.at(1), &y) || !readFinite(values.at(2), &z)) return false;
    *output = Vec3(x, y, z);
    return true;
}

void setError(QString* error, const QString& value) {
    if (error) *error = value;
}
}

EnvironmentParams::EnvironmentParams() {
    // 默认添加主方向光源与辅助点光源
    LightSource mainLight;
    mainLight.id = 1;
    mainLight.type = LightType::Directional;
    mainLight.position = Vec3(2.0f, 5.0f, 2.0f);
    mainLight.direction = Vec3(-0.35f, -0.85f, -0.45f).normalized();
    mainLight.color = Vec3(1.0f, 0.96f, 0.88f);
    mainLight.intensity = 0.8f;
    mainLight.enabled = true;
    lightSources.push_back(mainLight);

    LightSource fillLight;
    fillLight.id = 2;
    fillLight.type = LightType::Point;
    fillLight.position = Vec3(-3.0f, 4.0f, -1.0f);
    fillLight.direction = Vec3(0.6f, -0.8f, 0.2f).normalized();
    fillLight.color = Vec3(0.7f, 0.85f, 1.0f);
    fillLight.intensity = 0.4f;
    fillLight.enabled = true;
    lightSources.push_back(fillLight);
}

QJsonObject EnvironmentParams::toJson() const {
    QJsonArray lights;
    for (const LightSource& light : lightSources) {
        lights.append(QJsonObject{
            {QStringLiteral("id"), light.id},
            {QStringLiteral("type"), light.type == LightType::Point
                                         ? QStringLiteral("point")
                                         : QStringLiteral("directional")},
            {QStringLiteral("position"), vectorJson(light.position)},
            {QStringLiteral("direction"), vectorJson(light.direction)},
            {QStringLiteral("color"), vectorJson(light.color)},
            {QStringLiteral("intensity"), light.intensity},
            {QStringLiteral("enabled"), light.enabled}
        });
    }
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.environment")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("lightDirection"), vectorJson(lightDirection)},
        {QStringLiteral("lightIntensity"), lightIntensity},
        {QStringLiteral("moisture"), moisture},
        {QStringLiteral("nutrition"), nutrition},
        {QStringLiteral("temperature"), temperature},
        {QStringLiteral("windIntensity"), windIntensity},
        {QStringLiteral("time"), time},
        {QStringLiteral("phototropismWeight"), phototropismWeight},
        {QStringLiteral("gravitropismWeight"), gravitropismWeight},
        {QStringLiteral("occlusionRadius"), occlusionRadius},
        {QStringLiteral("shadowFactor"), shadowFactor},
        {QStringLiteral("lightSources"), lights}
    };
}

bool EnvironmentParams::fromJson(const QJsonObject& json, EnvironmentParams* output,
                                 QString* error) {
    if (!output) {
        setError(error, QStringLiteral("Output EnvironmentParams pointer is null."));
        return false;
    }
    if (json.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.environment") ||
        json.value(QStringLiteral("version")).toInt(-1) != 1) {
        setError(error, QStringLiteral("Unsupported or missing environment schema."));
        return false;
    }

    EnvironmentParams parsed;
    auto requiredNumber = [&](const char* key, float* value) {
        if (readFinite(json.value(QLatin1String(key)), value)) return true;
        setError(error, QStringLiteral("Environment field '%1' must be finite.").arg(QLatin1String(key)));
        return false;
    };
    if (!readVector(json.value(QStringLiteral("lightDirection")), &parsed.lightDirection) ||
        parsed.lightDirection.squaredNorm() < kEpsilon) {
        setError(error, QStringLiteral("Environment lightDirection must be a non-zero 3D vector."));
        return false;
    }
    parsed.lightDirection.normalize();
    if (!requiredNumber("lightIntensity", &parsed.lightIntensity) ||
        !requiredNumber("moisture", &parsed.moisture) ||
        !requiredNumber("nutrition", &parsed.nutrition) ||
        !requiredNumber("temperature", &parsed.temperature) ||
        !requiredNumber("windIntensity", &parsed.windIntensity) ||
        !requiredNumber("time", &parsed.time) ||
        !requiredNumber("phototropismWeight", &parsed.phototropismWeight) ||
        !requiredNumber("gravitropismWeight", &parsed.gravitropismWeight) ||
        !requiredNumber("occlusionRadius", &parsed.occlusionRadius) ||
        !requiredNumber("shadowFactor", &parsed.shadowFactor)) return false;

    parsed.lightIntensity = std::clamp(parsed.lightIntensity, 0.0f, 1.0f);
    parsed.moisture = std::clamp(parsed.moisture, 0.0f, 1.0f);
    parsed.nutrition = std::clamp(parsed.nutrition, 0.0f, 1.0f);
    parsed.windIntensity = std::max(0.0f, parsed.windIntensity);
    parsed.phototropismWeight = std::clamp(parsed.phototropismWeight, 0.0f, 1.0f);
    parsed.gravitropismWeight = std::clamp(parsed.gravitropismWeight, 0.0f, 1.0f);
    parsed.occlusionRadius = std::max(0.001f, parsed.occlusionRadius);
    parsed.shadowFactor = std::clamp(parsed.shadowFactor, 0.0f, 1.0f);

    const QJsonValue lightSourcesValue = json.value(QStringLiteral("lightSources"));
    if (!lightSourcesValue.isArray()) {
        setError(error, QStringLiteral("Environment lightSources must be an array."));
        return false;
    }
    parsed.lightSources.clear();
    for (const QJsonValue& value : lightSourcesValue.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("Each light source must be an object."));
            return false;
        }
        const QJsonObject source = value.toObject();
        LightSource light;
        light.id = source.value(QStringLiteral("id")).toInt(-1);
        const QString type = source.value(QStringLiteral("type")).toString().toLower();
        if (light.id < 0 || (type != QStringLiteral("directional") && type != QStringLiteral("point")) ||
            !readVector(source.value(QStringLiteral("position")), &light.position) ||
            !readVector(source.value(QStringLiteral("direction")), &light.direction) ||
            !readVector(source.value(QStringLiteral("color")), &light.color) ||
            !readFinite(source.value(QStringLiteral("intensity")), &light.intensity)) {
            setError(error, QStringLiteral("A light source contains invalid fields."));
            return false;
        }
        light.type = type == QStringLiteral("point") ? LightType::Point : LightType::Directional;
        if (light.direction.squaredNorm() < kEpsilon) {
            setError(error, QStringLiteral("Light source direction must be non-zero."));
            return false;
        }
        light.direction.normalize();
        light.color = light.color.cwiseMax(0.0f).cwiseMin(1.0f);
        light.intensity = std::max(0.0f, light.intensity);
        light.enabled = source.value(QStringLiteral("enabled")).toBool(true);
        parsed.lightSources.push_back(light);
    }
    *output = std::move(parsed);
    return true;
}

Vec3 EnvironmentParams::calculateEffectiveLightDirection(const Vec3& position, float* outTotalIntensity) const {
    Vec3 accumulatedDirection = Vec3::Zero();
    float totalWeight = 0.0f;

    for (const auto& light : lightSources) {
        if (!light.enabled || light.intensity <= 0.0f) continue;

        Vec3 incidentDir;
        float weight = light.intensity;

        if (light.type == LightType::Directional) {
            incidentDir = -light.direction.normalized();
        } else { // LightType::Point
            Vec3 toLight = light.position - position;
            float dist = toLight.norm();
            if (dist < kEpsilon) continue;
            incidentDir = toLight / dist;
            // 距离二次衰减
            float attenuation = 1.0f / (1.0f + 0.12f * dist + 0.04f * dist * dist);
            weight *= attenuation;
        }

        accumulatedDirection += incidentDir * weight;
        totalWeight += weight;
    }

    // 兼容全局默认单光源
    if (totalWeight < kEpsilon && lightIntensity > 0.0f) {
        Vec3 incidentDir = -lightDirection.normalized();
        accumulatedDirection = incidentDir * lightIntensity;
        totalWeight = lightIntensity;
    }

    if (outTotalIntensity) {
        *outTotalIntensity = totalWeight;
    }

    return accumulatedDirection.squaredNorm() > kEpsilon ? accumulatedDirection.normalized() : Vec3::UnitY();
}

float EnvironmentParams::calculateLightExposure(const Vec3& position, const std::vector<Vec3>& occluderPositions) const {
    if (occluderPositions.empty()) return 1.0f;

    Vec3 lightVec = calculateEffectiveLightDirection(position);
    float shadowCount = 0.0f;
    int testedSources = 0;

    for (const auto& light : lightSources) {
        if (!light.enabled) continue;
        testedSources++;

        Vec3 lightPos = (light.type == LightType::Point)
                            ? light.position
                            : (position + lightVec * 10.0f);

        Vec3 rayDir = (lightPos - position);
        float rayLen = rayDir.norm();
        if (rayLen < kEpsilon) continue;
        rayDir.normalize();

        // 沿光线射线检测是否有其他节点遮挡
        for (const Vec3& occluder : occluderPositions) {
            Vec3 toOccluder = occluder - position;
            float proj = toOccluder.dot(rayDir);
            // 只考虑位于节点上方/光线前方的遮挡体
            if (proj > 0.05f && proj < rayLen) {
                Vec3 closestPoint = position + rayDir * proj;
                float distSq = (occluder - closestPoint).squaredNorm();
                if (distSq < occlusionRadius * occlusionRadius) {
                    shadowCount += 1.0f;
                    break; // 此光源已被遮挡
                }
            }
        }
    }

    if (testedSources == 0) return 1.0f;
    float occlusionRatio = shadowCount / static_cast<float>(testedSources);
    float exposure = 1.0f - occlusionRatio * (1.0f - shadowFactor);
    return std::clamp(exposure, 0.05f, 1.0f);
}
