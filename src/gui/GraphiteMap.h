#pragma once

#include <QString>
#include <QWidget>

namespace opennord {

class GraphiteMap final : public QWidget
{
public:
    explicit GraphiteMap(QWidget *parent = nullptr);

    void setLocation(const QString &countryCode, const QString &countryName);
    void setConnected(bool connected);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString countryCode_;
    QString countryName_;
    bool connected_{};
};

} // namespace opennord
