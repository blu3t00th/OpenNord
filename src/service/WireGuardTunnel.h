#pragma once

#include "common/Models.h"

#include <QString>

namespace opennord {

class WireGuardTunnel final
{
public:
    WireGuardTunnel();

    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool connected() const;
    [[nodiscard]] QString connect(const Credentials &credentials, const Server &server, const Settings &settings);
    [[nodiscard]] QString disconnect();
    [[nodiscard]] QString executable() const { return executable_; }
    [[nodiscard]] QString configPath() const { return configPath_; }

private:
    [[nodiscard]] bool serviceExists() const;
    [[nodiscard]] QString run(const QStringList &arguments, int timeoutMs) const;
    QString executable_;
    QString configPath_;
};

}

