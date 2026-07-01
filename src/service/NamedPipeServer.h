#pragma once

#include "service/VpnController.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace opennord {

class NamedPipeServer final
{
public:
    explicit NamedPipeServer(VpnController &controller);
    ~NamedPipeServer();
    NamedPipeServer(const NamedPipeServer &) = delete;
    NamedPipeServer &operator=(const NamedPipeServer &) = delete;

    [[nodiscard]] bool start(QString &error);
    void stop();

private:
    void acceptLoop(std::stop_token stopToken);
    void serveClient(void *rawPipe);

    VpnController &controller_;
    std::atomic_bool running_{};
    std::jthread acceptThread_;
    struct ClientWorker {
        std::jthread thread;
        std::shared_ptr<std::atomic_bool> done;
    };
    std::mutex clientsMutex_;
    std::vector<ClientWorker> clients_;
};

}
