#include <lumora/ui/OrientationPresentation.hpp>

#include <QCoreApplication>
#include <QStringList>

namespace lumora::ui {

QString orientationDescription(core::Orientation orientation) {
    QStringList parts;
    if (orientation.flipHorizontal)
        parts.push_back(QCoreApplication::translate("Orientation", "H flip"));
    if (orientation.flipVertical)
        parts.push_back(QCoreApplication::translate("Orientation", "V flip"));
    switch (orientation.rotation) {
    case core::Rotation::Degrees0: parts.push_back(QStringLiteral("0°")); break;
    case core::Rotation::Degrees90: parts.push_back(QStringLiteral("90°")); break;
    case core::Rotation::Degrees180: parts.push_back(QStringLiteral("180°")); break;
    case core::Rotation::Degrees270: parts.push_back(QStringLiteral("270°")); break;
    }
    return parts.join(QStringLiteral(" · "));
}

}  // namespace lumora::ui
