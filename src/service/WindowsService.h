#pragma once

#include "common/Protocol.h"
#include "service/VpnController.h"

#include <QString>

namespace opennord {

class WindowsService final
{
public:
    static constexpr auto Name = protocol::ServiceName;

    [[nodiscard]] static int dispatch(VpnController &controller);
    [[nodiscard]] static QString install();
    [[nodiscard]] static QString uninstall();
    [[nodiscard]] static QString start();
    [[nodiscard]] static QString stop();
    [[nodiscard]] static QString restart();
};

}
