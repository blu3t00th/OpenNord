#pragma once

#include "common/Models.h"

#include <QString>

namespace opennord {

struct ConfigResult {
    QString value;
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

[[nodiscard]] ConfigResult buildWireGuardConfig(
    const Credentials &credentials,
    const Server &server,
    const Settings &settings);

}

