#include "service/NamedPipeServer.h"

#include "common/Logging.h"
#include "common/Protocol.h"
#include "windows/Security.h"
#include "windows/WinHandle.h"

#include <QJsonDocument>
#include <QScopeGuard>
#include <QtEndian>

#include <windows.h>

#include <algorithm>
#include <exception>

namespace opennord {
namespace {

void log(const QString &message)
{
    logging::write(logging::Target::Service, QStringLiteral("named-pipe"), message);
}

bool readExact(HANDLE pipe, char *data, DWORD size)
{
    DWORD offset{};
    while (offset < size) {
        DWORD read{};
        if (!ReadFile(pipe, data + offset, size - offset, &read, nullptr) || read == 0) return false;
        offset += read;
    }
    return true;
}

bool writeAll(HANDLE pipe, const QByteArray &data)
{
    DWORD offset{};
    while (offset < static_cast<DWORD>(data.size())) {
        DWORD written{};
        if (!WriteFile(pipe, data.constData() + offset, static_cast<DWORD>(data.size()) - offset, &written, nullptr) || written == 0) return false;
        offset += written;
    }
    return FlushFileBuffers(pipe) != FALSE;
}

}

NamedPipeServer::NamedPipeServer(VpnController &controller) : controller_(controller) {}
NamedPipeServer::~NamedPipeServer() { stop(); }

bool NamedPipeServer::start(QString &error)
{
    if (running_.exchange(true)) return true;
    PSECURITY_DESCRIPTOR descriptor{};
    [[maybe_unused]] const auto attributes = windows::pipeSecurityAttributes(descriptor, error);
    if (!descriptor) {
        log(QStringLiteral("cannot initialize pipe security: %1").arg(error));
        running_ = false;
        return false;
    }
    LocalFree(descriptor);
    acceptThread_ = std::jthread([this](std::stop_token token) { acceptLoop(token); });
    error.clear();
    return true;
}

void NamedPipeServer::stop()
{
    if (!running_.exchange(false)) return;
    acceptThread_.request_stop();
    windows::UniqueHandle wake(CreateFileW(protocol::PipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (acceptThread_.joinable()) acceptThread_.join();
    std::scoped_lock lock(clientsMutex_);
    for (auto &client : clients_) {
        client.thread.request_stop();
        CancelSynchronousIo(reinterpret_cast<HANDLE>(client.thread.native_handle()));
    }
    clients_.clear();
}

void NamedPipeServer::acceptLoop(std::stop_token stopToken)
{
    while (running_ && !stopToken.stop_requested()) {
        QString securityError;
        PSECURITY_DESCRIPTOR descriptor{};
        auto attributes = windows::pipeSecurityAttributes(descriptor, securityError);
        if (!descriptor) {
            log(QStringLiteral("cannot create pipe security descriptor: %1").arg(securityError));
            return;
        }
        windows::UniqueHandle pipe(CreateNamedPipeW(
            protocol::PipeName,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            16,
            protocol::MaxFrameSize + 4,
            protocol::MaxFrameSize + 4,
            0,
            &attributes));
        LocalFree(descriptor);
        if (!pipe) {
            log(QStringLiteral("CreateNamedPipe failed: %1").arg(windows::lastErrorMessage()));
            return;
        }
        const auto connected = ConnectNamedPipe(pipe.get(), nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
        if (!connected) continue;
        if (!running_ || stopToken.stop_requested()) return;
        auto done = std::make_shared<std::atomic_bool>(false);
        std::scoped_lock lock(clientsMutex_);
        std::erase_if(clients_, [](const ClientWorker &worker) { return worker.done->load(); });
        auto *rawPipe = pipe.release();
        clients_.push_back(ClientWorker{
            std::jthread([this, rawPipe, done](std::stop_token) {
                serveClient(rawPipe);
                done->store(true);
            }),
            std::move(done),
        });
    }
}

void NamedPipeServer::serveClient(void *rawPipe)
{
    windows::UniqueHandle pipe(static_cast<HANDLE>(rawPipe));
    const auto disconnect = qScopeGuard([&] { DisconnectNamedPipe(pipe.get()); });

    quint32 encodedLength{};
    if (!readExact(pipe.get(), reinterpret_cast<char *>(&encodedLength), sizeof(encodedLength))) {
        log(QStringLiteral("client disconnected before sending a frame header"));
        return;
    }
    const auto length = qFromLittleEndian(encodedLength);
    if (length == 0 || length > protocol::MaxFrameSize) {
        log(QStringLiteral("client sent invalid frame length %1").arg(length));
        return;
    }
    QByteArray payload(static_cast<qsizetype>(length), Qt::Uninitialized);
    if (!readExact(pipe.get(), payload.data(), length)) {
        log(QStringLiteral("client disconnected before completing its frame"));
        return;
    }

    const auto identity = windows::namedPipeClientSid(pipe.get());
    if (!identity.ok()) {
        log(QStringLiteral("cannot authenticate pipe client: %1").arg(identity.error));
        return;
    }

    QJsonParseError parseError;
    const auto request = QJsonDocument::fromJson(payload, &parseError).object();
    const auto id = request.value(QStringLiteral("id")).toInteger(-1);
    QJsonObject response;
    if (parseError.error != QJsonParseError::NoError || id < 0) {
        response = protocol::failure(id, QStringLiteral("invalid_request"), QStringLiteral("malformed RPC request"));
    } else {
        const auto method = request.value(QStringLiteral("method")).toString();
        const auto params = request.value(QStringLiteral("params")).toObject();
        try {
            const auto result = controller_.handle(ClientContext{identity.value}, method, params);
            response = result.ok() ? protocol::success(id, result.value) : protocol::failure(id, result.code, result.error);
        } catch (const std::exception &error) {
            response = protocol::failure(id, QStringLiteral("internal_error"),
                QStringLiteral("service request failed: %1").arg(QString::fromLocal8Bit(error.what())));
        } catch (...) {
            response = protocol::failure(id, QStringLiteral("internal_error"), QStringLiteral("service request failed unexpectedly"));
        }
    }
    if (!writeAll(pipe.get(), protocol::encodeFrame(response))) {
        log(QStringLiteral("failed to write RPC response for request %1").arg(id));
    }
}

}
