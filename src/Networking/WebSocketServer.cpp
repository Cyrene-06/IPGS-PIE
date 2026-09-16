// ============================================================================
// WebSocketServer implementation
// ============================================================================
#include "Networking/WebSocketServer.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <QWebSocketServer>

#include "Engine/SimulationEngine.h"

namespace {
constexpr int kProtocolVersion = 1;
constexpr int kGrowthBroadcastIntervalMs = 66;
constexpr int kHeartbeatIntervalMs = 15000;
constexpr int kHeartbeatTimeoutMs = 45000;
constexpr int kCompressionThresholdBytes = 4096;
constexpr char kCompressedPayloadMagic[] = "PSZ1";

float clampLight(float value) {
    return qBound(0.0f, value, 1.0f);
}

bool readFiniteFloat(const QJsonValue& value, float* output) {
    if (!output || !value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number)) return false;
    *output = static_cast<float>(number);
    return std::isfinite(*output);
}

bool readVec3(const QJsonValue& value, Vec3* output) {
    if (!output) return false;
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (array.size() != 3 || !readFiniteFloat(array.at(0), &x) ||
            !readFiniteFloat(array.at(1), &y) || !readFiniteFloat(array.at(2), &z)) return false;
        *output = Vec3(x, y, z);
        return true;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!readFiniteFloat(object.value("x"), &x) || !readFiniteFloat(object.value("y"), &y) ||
            !readFiniteFloat(object.value("z"), &z)) return false;
        *output = Vec3(x, y, z);
        return true;
    }
    return false;
}

bool readOptionalFloat(const QJsonObject& object, const char* key, std::optional<float>* output) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull()) return true;
    float number = 0.0f;
    if (!readFiniteFloat(value, &number)) return false;
    *output = number;
    return true;
}

bool readOptionalInt(const QJsonObject& object, const char* key, std::optional<int>* output) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined() || value.isNull() || !value.isDouble()) return value.isUndefined() || value.isNull();
    const double number = value.toDouble();
    if (!std::isfinite(number) || number != static_cast<double>(static_cast<int>(number))) return false;
    *output = static_cast<int>(number);
    return true;
}
}

WebSocketServer::WebSocketServer(QObject* parent)
    : QObject(parent),
      server_(new QWebSocketServer(QStringLiteral("PlantSim local control"),
                                   QWebSocketServer::NonSecureMode, this)) {
    growthBroadcastTimer_.setInterval(kGrowthBroadcastIntervalMs);
    growthBroadcastTimer_.setSingleShot(true);
    heartbeatTimer_.setInterval(kHeartbeatIntervalMs);

    connect(server_, &QWebSocketServer::newConnection,
            this, &WebSocketServer::onNewConnection);
    connect(&growthBroadcastTimer_, &QTimer::timeout,
            this, &WebSocketServer::flushPendingGrowthState);
    connect(&heartbeatTimer_, &QTimer::timeout,
            this, &WebSocketServer::sendHeartbeat);
    initializeCommandHandlers();
}

bool WebSocketServer::listen(quint16 port) {
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        emit logMessage(QStringLiteral("WebSocket listen failed: %1").arg(server_->errorString()));
        return false;
    }
    port_ = server_->serverPort();
    heartbeatTimer_.start();
    emit logMessage(QStringLiteral("WebSocket listening on ws://127.0.0.1:%1").arg(port_));
    return true;
}

quint16 WebSocketServer::port() const {
    return port_;
}

void WebSocketServer::setSimulationEngine(SimulationEngine* engine) {
    simulationEngine_ = engine;
}

void WebSocketServer::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QWebSocket* socket = server_->nextPendingConnection();
        socket->setParent(this);
        clients_.insert(socket, {QDateTime::currentDateTimeUtc()});
        connect(socket, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessageReceived);
        connect(socket, &QWebSocket::pong, this, &WebSocketServer::onPong);
        connect(socket, &QWebSocket::disconnected,
                this, &WebSocketServer::onDisconnected);
        emit clientConnected();
        emit logMessage(QStringLiteral("WebSocket client connected"));
    }
}

void WebSocketServer::onTextMessageReceived(const QString& message) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket || !clients_.contains(socket)) {
        return;
    }
    clients_[socket].lastPongUtc = QDateTime::currentDateTimeUtc();
    processTextMessage(socket, message.toUtf8());
}

void WebSocketServer::onPong(quint64, const QByteArray&) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket && clients_.contains(socket)) {
        clients_[socket].lastPongUtc = QDateTime::currentDateTimeUtc();
    }
}

void WebSocketServer::initializeCommandHandlers() {
    commandHandlers_.insert(QStringLiteral("adjust_light"), [this](QWebSocket*, const QJsonObject& command) {
        emit lightIntensityRequested(clampLight(static_cast<float>(command.value("value").toDouble(0.0))));
    });
    commandHandlers_.insert(QStringLiteral("set_phototropism"), [this](QWebSocket*, const QJsonObject& command) {
        emit phototropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
    });
    commandHandlers_.insert(QStringLiteral("set_gravitropism"), [this](QWebSocket*, const QJsonObject& command) {
        emit gravitropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
    });
    commandHandlers_.insert(QStringLiteral("set_light_position"), [this](QWebSocket*, const QJsonObject& command) {
        emit lightPositionRequested(command.value("id").toInt(0),
                                    static_cast<float>(command.value("x").toDouble(0.0)),
                                    static_cast<float>(command.value("y").toDouble(0.0)),
                                    static_cast<float>(command.value("z").toDouble(0.0)));
    });
    commandHandlers_.insert(QStringLiteral("growth_start"), [this](QWebSocket*, const QJsonObject&) { emit growthStartRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_pause"), [this](QWebSocket*, const QJsonObject&) { emit growthPauseRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_resume"), [this](QWebSocket*, const QJsonObject&) { emit growthResumeRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_reset"), [this](QWebSocket*, const QJsonObject&) { emit growthResetRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_seek"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthSeekRequested(std::max(0.0f, static_cast<float>(command.value("age").toDouble(0.0))));
    });
    commandHandlers_.insert(QStringLiteral("growth_stage"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthStageRequested(command.value("stage").toString());
    });
    commandHandlers_.insert(QStringLiteral("growth_speed"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthSpeedRequested(qBound(0.1f, static_cast<float>(command.value("speed").toDouble(1.0)), 8.0f));
    });
    commandHandlers_.insert(QStringLiteral("request_growth_data"), [this](QWebSocket*, const QJsonObject&) {
        emit growthDataRequested();
    });
    commandHandlers_.insert(QStringLiteral("persistence.preset.request"), [this](QWebSocket* socket, const QJsonObject& command) {
        if (!simulationEngine_) {
            sendError(socket, QStringLiteral("engine_unavailable"), QStringLiteral("Simulation engine is not configured."));
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("persistence.preset")},
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
            {QStringLiteral("preset"), simulationEngine_->createPreset()}
        }).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("persistence.preset.import"), [this](QWebSocket* socket, const QJsonObject& command) {
        QString error;
        if (!simulationEngine_ || !command.value(QStringLiteral("preset")).isObject() ||
            !simulationEngine_->applyPreset(command.value(QStringLiteral("preset")).toObject(), &error)) {
            sendError(socket, QStringLiteral("preset_import_failed"),
                      error.isEmpty() ? QStringLiteral("A valid preset object is required.") : error);
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("persistence.updated")},
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
            {QStringLiteral("action"), QStringLiteral("preset_imported")},
            {QStringLiteral("age"), simulationEngine_->growthClock().timeline().currentAge()}
        }).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("persistence.scene.request"), [this](QWebSocket* socket, const QJsonObject& command) {
        if (!simulationEngine_) {
            sendError(socket, QStringLiteral("engine_unavailable"), QStringLiteral("Simulation engine is not configured."));
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("persistence.scene")},
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
            {QStringLiteral("scene"), simulationEngine_->createSceneArchive()}
        }).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("persistence.scene.import"), [this](QWebSocket* socket, const QJsonObject& command) {
        QString error;
        float age = -1.0f;
        if (command.contains(QStringLiteral("age")) &&
            (!readFiniteFloat(command.value(QStringLiteral("age")), &age) || age < 0.0f)) {
            sendError(socket, QStringLiteral("scene_import_failed"),
                      QStringLiteral("Optional scene import age must be a non-negative finite number."));
            return;
        }
        if (!simulationEngine_ || !command.value(QStringLiteral("scene")).isObject() ||
            !simulationEngine_->restoreSceneArchive(command.value(QStringLiteral("scene")).toObject(), age, &error)) {
            sendError(socket, QStringLiteral("scene_import_failed"),
                      error.isEmpty() ? QStringLiteral("A valid scene archive is required.") : error);
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("persistence.updated")},
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
            {QStringLiteral("action"), QStringLiteral("scene_imported")},
            {QStringLiteral("age"), simulationEngine_->growthClock().timeline().currentAge()}
        }).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("persistence.scene.restore"), [this](QWebSocket* socket, const QJsonObject& command) {
        QString error;
        float age = 0.0f;
        if (!readFiniteFloat(command.value(QStringLiteral("age")), &age) || age < 0.0f ||
            !simulationEngine_ || !simulationEngine_->restoreRecordedScene(age, &error)) {
            sendError(socket, QStringLiteral("scene_restore_failed"),
                      error.isEmpty() ? QStringLiteral("A non-negative restore age is required.") : error);
            return;
        }
        sendJson(socket, QJsonDocument(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("persistence.updated")},
            {QStringLiteral("protocolVersion"), kProtocolVersion},
            {QStringLiteral("requestId"), command.value(QStringLiteral("requestId"))},
            {QStringLiteral("action"), QStringLiteral("scene_restored")},
            {QStringLiteral("age"), simulationEngine_->growthClock().timeline().currentAge()}
        }).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("persistence.obj.request"), [this](QWebSocket* socket, const QJsonObject& command) {
        QString error;
        const QJsonObject bundle = simulationEngine_ ? simulationEngine_->createSceneExportBundle(&error)
                                                     : QJsonObject{};
        if (bundle.isEmpty()) {
            sendError(socket, QStringLiteral("scene_export_failed"),
                      error.isEmpty() ? QStringLiteral("Simulation engine is not configured.") : error);
            return;
        }
        QJsonObject response = bundle;
        response.insert(QStringLiteral("type"), QStringLiteral("persistence.obj"));
        response.insert(QStringLiteral("protocolVersion"), kProtocolVersion);
        response.insert(QStringLiteral("requestId"), command.value(QStringLiteral("requestId")));
        sendJson(socket, QJsonDocument(response).toJson(QJsonDocument::Compact));
    });
    commandHandlers_.insert(QStringLiteral("edit.begin"), [this](QWebSocket* socket, const QJsonObject& command) {
        int nodeId = -1;
        QString error;
        if (!resolveEditNode(command, &nodeId, &error)) {
            sendError(socket, QStringLiteral("invalid_edit_target"), error);
            return;
        }
        const QString tool = command.value("tool").toString().trimmed().toLower();
        if (tool != QStringLiteral("pick") && tool != QStringLiteral("scale") &&
            tool != QStringLiteral("bend") && tool != QStringLiteral("rotate") &&
            tool != QStringLiteral("parameter")) {
            sendError(socket, QStringLiteral("invalid_edit_tool"), QStringLiteral("Unsupported edit tool: %1").arg(tool));
            return;
        }
        if (tool != QStringLiteral("pick")) simulationEngine_->beginEdit();
        sendEditUpdated(socket, command, nodeId, false);
    });
    commandHandlers_.insert(QStringLiteral("edit.update"), [this](QWebSocket* socket, const QJsonObject& command) {
        int nodeId = -1;
        QString error;
        if (!resolveEditNode(command, &nodeId, &error)) {
            sendError(socket, QStringLiteral("invalid_edit_target"), error);
            return;
        }
        const QString tool = command.value("tool").toString().trimmed().toLower();
        const QJsonObject params = command.value("params").toObject();
        if (tool.isEmpty() || params.isEmpty()) {
            sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("edit.update requires a tool and params object."));
            return;
        }
        const bool preview = command.value("preview").toBool(true);
        bool accepted = false;
        if (tool == QStringLiteral("scale")) {
            ScaleParams scale;
            const QJsonValue rawScale = params.value("scale");
            if (rawScale.isDouble()) {
                float uniform = 1.0f;
                if (!readFiniteFloat(rawScale, &uniform)) {
                    sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("scale must be finite."));
                    return;
                }
                scale.scale = Vec3::Constant(uniform);
            } else if (!readVec3(rawScale, &scale.scale)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("scale must be a number or {x,y,z}."));
                return;
            }
            const QString axis = params.value("axis").toString().trimmed().toLower();
            if (rawScale.isDouble() && axis == QStringLiteral("x")) scale.scale = Vec3(scale.scale.x(), 1.0f, 1.0f);
            else if (rawScale.isDouble() && axis == QStringLiteral("y")) scale.scale = Vec3(1.0f, scale.scale.x(), 1.0f);
            else if (rawScale.isDouble() && axis == QStringLiteral("z")) scale.scale = Vec3(1.0f, 1.0f, scale.scale.x());
            if (!readFiniteFloat(params.value("minimumRadius"), &scale.minimumRadius) && !params.value("minimumRadius").isUndefined()) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("minimumRadius must be finite.")); return;
            }
            if (!readFiniteFloat(params.value("minimumLength"), &scale.minimumLength) && !params.value("minimumLength").isUndefined()) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("minimumLength must be finite.")); return;
            }
            scale.scaleLeaves = params.value("scaleLeaves").toBool(true);
            accepted = simulationEngine_->applyScaleEdit(nodeId, scale, preview, &error);
        } else if (tool == QStringLiteral("bend")) {
            BendParams bend;
            if (!readVec3(params.value("axis"), &bend.bendAxis)) bend.bendAxis = Vec3::UnitZ();
            float degrees = 0.0f;
            if (params.contains("angleDegrees")) {
                if (!readFiniteFloat(params.value("angleDegrees"), &degrees)) {
                    sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("angleDegrees must be finite.")); return;
                }
                bend.angleRadians = degrees * 0.01745329251994329577f;
            } else if (!readFiniteFloat(params.value("angleRadians"), &bend.angleRadians)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("bend requires angleDegrees or angleRadians.")); return;
            }
            if (!readFiniteFloat(params.value("falloff"), &bend.falloff) && !params.value("falloff").isUndefined()) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("falloff must be finite.")); return;
            }
            if (!readFiniteFloat(params.value("stiffness"), &bend.stiffness) && !params.value("stiffness").isUndefined()) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("stiffness must be finite.")); return;
            }
            accepted = simulationEngine_->applyBendEdit(nodeId, bend, preview, &error);
        } else if (tool == QStringLiteral("rotate")) {
            Vec3 axis = Vec3::UnitY();
            if (params.contains("axis") && !readVec3(params.value("axis"), &axis)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("axis must be a 3D vector.")); return;
            }
            float angleRadians = 0.0f;
            if (params.contains("angleDegrees")) {
                float degrees = 0.0f;
                if (!readFiniteFloat(params.value("angleDegrees"), &degrees)) {
                    sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("angleDegrees must be finite.")); return;
                }
                angleRadians = degrees * 0.01745329251994329577f;
            } else if (!readFiniteFloat(params.value("angleRadians"), &angleRadians)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("rotate requires angleDegrees or angleRadians.")); return;
            }
            const PlantNode* node = simulationEngine_->plantModel().findNode(nodeId);
            Vec3 pivot = node ? node->position : Vec3::Zero();
            if (params.contains("pivot") && !readVec3(params.value("pivot"), &pivot)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("pivot must be a 3D vector.")); return;
            }
            accepted = simulationEngine_->applyRotateEdit(nodeId, pivot, axis, angleRadians, preview, &error);
        } else if (tool == QStringLiteral("parameter")) {
            NodeParameterUpdate update;
            if (!readOptionalFloat(params, "angleDegrees", &update.angleDegrees) ||
                !readOptionalFloat(params, "length", &update.length) ||
                !readOptionalFloat(params, "radius", &update.radius) ||
                !readOptionalFloat(params, "leafDensity", &update.leafDensity) ||
                !readOptionalFloat(params, "age", &update.age) ||
                !readOptionalInt(params, "growthDepth", &update.growthDepth)) {
                sendError(socket, QStringLiteral("invalid_edit_params"), QStringLiteral("Parameter edit values must be finite numbers."));
                return;
            }
            accepted = simulationEngine_->applyNodeParameterEdit(nodeId, update, preview, &error);
        } else {
            sendError(socket, QStringLiteral("invalid_edit_tool"), QStringLiteral("Unsupported edit tool: %1").arg(tool));
            return;
        }
        if (!accepted) {
            sendError(socket, QStringLiteral("edit_rejected"), error.isEmpty() ? QStringLiteral("The edit was rejected.") : error);
            return;
        }
        sendEditUpdated(socket, command, nodeId, true);
    });
    commandHandlers_.insert(QStringLiteral("edit.commit"), [this](QWebSocket* socket, const QJsonObject& command) {
        int nodeId = -1;
        QString error;
        if (!resolveEditNode(command, &nodeId, &error)) { sendError(socket, QStringLiteral("invalid_edit_target"), error); return; }
        const bool changed = simulationEngine_->commitEdit();
        sendEditUpdated(socket, command, nodeId, changed);
    });
    commandHandlers_.insert(QStringLiteral("edit.undo"), [this](QWebSocket* socket, const QJsonObject& command) {
        QString error;
        if (!simulationEngine_ || !simulationEngine_->undoLastEdit(&error)) {
            sendError(socket, QStringLiteral("nothing_to_undo"), error.isEmpty() ? QStringLiteral("No committed edit is available to undo.") : error);
            return;
        }
        sendEditUpdated(socket, command, simulationEngine_->plantModel().rootNodeId(), true);
    });
    commandHandlers_.insert(QStringLiteral("edit.reset"), [this](QWebSocket* socket, const QJsonObject& command) {
        if (!simulationEngine_) { sendError(socket, QStringLiteral("engine_unavailable"), QStringLiteral("Simulation engine is not configured.")); return; }
        simulationEngine_->resetPlant();
        sendEditUpdated(socket, command, simulationEngine_->plantModel().rootNodeId(), true);
    });
    commandHandlers_.insert(QStringLiteral("ping"), [this](QWebSocket* socket, const QJsonObject&) {
        sendJson(socket, QJsonDocument(QJsonObject{{"type", "pong"}, {"protocolVersion", kProtocolVersion}})
                             .toJson(QJsonDocument::Compact));
    });
}

void WebSocketServer::sendError(QWebSocket* socket, const QString& code, const QString& message) {
    sendJson(socket, QJsonDocument(QJsonObject{
        {"type", "error"}, {"protocolVersion", kProtocolVersion},
        {"code", code}, {"message", message}
    }).toJson(QJsonDocument::Compact));
}

void WebSocketServer::processTextMessage(QWebSocket* socket, const QByteArray& payload) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendError(socket, QStringLiteral("invalid_json"), QStringLiteral("Command must be a JSON object."));
        return;
    }

    const QJsonObject command = document.object();
    const QJsonValue protocolValue = command.value("protocolVersion");
    if (!protocolValue.isDouble() || protocolValue.toDouble() != static_cast<double>(protocolValue.toInt())) {
        sendError(socket, QStringLiteral("invalid_protocol_version"),
                  QStringLiteral("protocolVersion must be an integer."));
        return;
    }
    if (protocolValue.toInt() != kProtocolVersion) {
        sendError(socket, QStringLiteral("unsupported_protocol_version"),
                  QStringLiteral("This server supports protocolVersion %1.").arg(kProtocolVersion));
        return;
    }

    const QString type = command.value("type").toString();
    if (type.isEmpty()) {
        sendError(socket, QStringLiteral("invalid_command"), QStringLiteral("Command type is required."));
        return;
    }
    const auto handler = commandHandlers_.constFind(type);
    if (handler == commandHandlers_.cend()) {
        sendError(socket, QStringLiteral("unknown_command"),
                  QStringLiteral("Unsupported command type: %1").arg(type));
        return;
    }
    (*handler)(socket, command);
}

bool WebSocketServer::resolveEditNode(const QJsonObject& command, int* nodeId, QString* error) const {
    if (!simulationEngine_) {
        if (error) *error = QStringLiteral("Simulation engine is not configured.");
        return false;
    }
    const QJsonValue plantIdValue = command.value("plantId");
    if (!plantIdValue.isDouble() || plantIdValue.toInt() != simulationEngine_->plantModel().id) {
        if (error) *error = QStringLiteral("plantId must identify the active plant.");
        return false;
    }
    const QString mode = command.value("mode").toString().trimmed().toLower();
    int resolved = -1;
    if (mode == QStringLiteral("whole") || mode == QStringLiteral("wholeplant")) {
        resolved = simulationEngine_->plantModel().rootNodeId();
    } else {
        const QJsonValue nodeValue = command.value("nodeId");
        if (!nodeValue.isDouble()) {
            if (error) *error = QStringLiteral("nodeId is required for node editing.");
            return false;
        }
        resolved = nodeValue.toInt(-1);
    }
    if (!simulationEngine_->plantModel().findNode(resolved)) {
        if (error) *error = QStringLiteral("Node %1 does not exist in the active plant.").arg(resolved);
        return false;
    }
    if (nodeId) *nodeId = resolved;
    return true;
}

void WebSocketServer::sendEditUpdated(QWebSocket* socket, const QJsonObject& command,
                                      int nodeId, bool rebuildCompleted) {
    if (!simulationEngine_) return;
    QJsonObject response{
        {"type", "plant.edit.updated"},
        {"protocolVersion", kProtocolVersion},
        {"plantId", simulationEngine_->plantModel().id},
        {"nodeId", nodeId},
        {"revision", static_cast<qint64>(simulationEngine_->editRevision())},
        {"meshVersion", static_cast<qint64>(simulationEngine_->meshVersion())},
        {"rebuildCompleted", rebuildCompleted},
        {"canUndo", simulationEngine_->canUndo()}
    };
    const QString requestId = command.value("requestId").toString();
    if (!requestId.isEmpty()) response.insert("requestId", requestId);
    sendJson(socket, QJsonDocument(response).toJson(QJsonDocument::Compact));
}

void WebSocketServer::sendJson(QWebSocket* socket, const QByteArray& payload) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // Qt WebSockets handles RFC 6455 masking, fragmentation, control frames and
    // multi-client lifecycle. Large state archives are additionally sent as a
    // compact binary qCompress payload (PSZ1 + qCompress JSON) to save localhost
    // bandwidth and browser JSON parsing pressure.
    if (payload.size() >= kCompressionThresholdBytes) {
        socket->sendBinaryMessage(QByteArray(kCompressedPayloadMagic, 4) + qCompress(payload, 6));
    } else {
        socket->sendTextMessage(QString::fromUtf8(payload));
    }
}

void WebSocketServer::broadcastJson(const QByteArray& payload) {
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        sendJson(it.key(), payload);
    }
}

void WebSocketServer::broadcastState(float lightIntensity) {
    const QJsonObject state{
        {"type", "environment_updated"},
        {"protocolVersion", kProtocolVersion},
        {"message", "Environment Updated"},
        {"lightIntensity", static_cast<double>(lightIntensity)}
    };
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastTropismState(float photoWeight, float graviWeight) {
    const QJsonObject state{
        {"type", "tropism_updated"},
        {"protocolVersion", kProtocolVersion},
        {"phototropismWeight", static_cast<double>(photoWeight)},
        {"gravitropismWeight", static_cast<double>(graviWeight)}
    };
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastGrowthState(const GrowthStateReport& report) {
    if (!report.plantState.isEmpty()) {
        hasPendingGrowthReport_ = false;
        growthBroadcastTimer_.stop();
        broadcastGrowthStateNow(report);
        return;
    }

    pendingGrowthReport_ = report;
    hasPendingGrowthReport_ = true;
    if (!growthBroadcastTimer_.isActive()) {
        growthBroadcastTimer_.start();
    }
}

void WebSocketServer::flushPendingGrowthState() {
    if (!hasPendingGrowthReport_) {
        return;
    }
    broadcastGrowthStateNow(pendingGrowthReport_);
    hasPendingGrowthReport_ = false;
}

void WebSocketServer::broadcastGrowthStateNow(const GrowthStateReport& report) {
    const PlantGrowthMetrics& metrics = report.metrics;
    QJsonObject state{
        {"type", "growth_state"},
        {"protocolVersion", kProtocolVersion},
        {"age", static_cast<double>(report.age)},
        {"lifeStage", toString(report.lifeStage)},
        {"mode", report.mode},
        {"speed", static_cast<double>(report.speed)},
        {"nodeCount", report.nodeCount},
        {"branchCount", report.branchCount},
        {"leafCount", report.leafCount},
        {"height", static_cast<double>(metrics.height)},
        {"totalBranchLength", static_cast<double>(metrics.totalBranchLength)},
        {"canopyWidth", static_cast<double>(metrics.canopyWidth)},
        {"recordedFrameCount", report.recordedFrameCount},
        {"recordedEndAge", static_cast<double>(report.recordedEndAge)}
    };
    if (!report.plantState.isEmpty()) {
        state.insert("plantState", report.plantState);
    }
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastGrowthData(const QJsonObject& data) {
    QJsonObject payloadObject = data;
    payloadObject.insert("type", "growth_data");
    payloadObject.insert("protocolVersion", kProtocolVersion);
    broadcastJson(QJsonDocument(payloadObject).toJson(QJsonDocument::Compact));
}

void WebSocketServer::sendHeartbeat() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<QWebSocket*> expired;
    for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
        QWebSocket* socket = it.key();
        if (!socket || socket->state() != QAbstractSocket::ConnectedState ||
            it.value().lastPongUtc.msecsTo(now) > kHeartbeatTimeoutMs) {
            expired.push_back(socket);
            continue;
        }
        socket->ping("plantsim-heartbeat");
    }
    for (QWebSocket* socket : expired) {
        if (socket) {
            socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                          QStringLiteral("Heartbeat timeout"));
        }
    }
}

void WebSocketServer::onDisconnected() {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    removeClient(socket);
    emit clientDisconnected();
}

void WebSocketServer::removeClient(QWebSocket* socket) {
    if (!socket) return;
    clients_.remove(socket);
    socket->deleteLater();
}
