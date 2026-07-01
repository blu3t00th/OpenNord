#pragma once

#include "common/Models.h"

#include <QString>

namespace opennord {

template<typename T>
struct StoreResult {
    T value{};
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

class SecureStore final
{
public:
    SecureStore();

    [[nodiscard]] StoreResult<Session> load(const QString &sid) const;
    [[nodiscard]] QString save(const QString &sid, const Session &session) const;
    [[nodiscard]] QString remove(const QString &sid) const;
    [[nodiscard]] QString directory() const { return directory_; }

private:
    [[nodiscard]] QString pathFor(const QString &sid) const;
    QString directory_;
};

}

