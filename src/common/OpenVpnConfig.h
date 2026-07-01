#pragma once

#include "common/Models.h"
#include "common/WireGuardConfig.h"

#include <QByteArray>

namespace opennord {

[[nodiscard]] ConfigResult buildOpenVpnConfig(
    const QByteArray &signedProfile,
    const Server &server,
    const Settings &settings,
    quint16 managementPort,
    const QString &managementPasswordPath);

[[nodiscard]] QString openVpnManagementEscape(const QString &value);

}
