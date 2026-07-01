#pragma once

#include <QString>

namespace opennord::logging {

enum class Target { Gui, Service };

[[nodiscard]] QString path(Target target);
void write(Target target, const QString &area, const QString &message);

}
