#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>

#include <atomic>
#include <functional>

namespace opennord {

class RpcClient final : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(QJsonValue, QString)>;

    explicit RpcClient(QObject *parent = nullptr);
    void call(QString method, QJsonObject params, Callback callback);
    void setServiceAutoStartEnabled(bool enabled) { autoStartService_.store(enabled); }

private:
    [[nodiscard]] QJsonObject callBlocking(qint64 id, const QString &method, const QJsonObject &params,
                                           bool allowStatusRetry = true) const;
    qint64 nextId_{1};
    std::atomic_bool autoStartService_{true};
};

}
