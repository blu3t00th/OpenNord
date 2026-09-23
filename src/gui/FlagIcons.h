#pragma once

#include <QIcon>
#include <QString>

namespace opennord {

[[nodiscard]] bool hasCountryFlag(const QString &countryCode);
[[nodiscard]] QIcon countryFlagIcon(const QString &countryCode);

}
