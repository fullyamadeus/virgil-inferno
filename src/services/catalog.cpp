#include "services/catalog.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QResource>

static void initializeCatalog() { Q_INIT_RESOURCE(catalog); }

namespace virgil {
QList<CatalogCourse> courseCatalog()
{
    static const bool initialized = [] { initializeCatalog(); return true; }();
    Q_UNUSED(initialized);
    QFile file(":/catalog/finki-seis23.json");
    if (!file.open(QIODevice::ReadOnly)) return {}; // Manual entry always works.
    QList<CatalogCourse> result;
    const auto courses = QJsonDocument::fromJson(file.readAll()).object().value("courses").toArray();
    for (const auto& item : courses) {
        const auto course = item.toObject();
        result.append({course.value("code").toString(), course.value("name").toString(),
                       course.value("semester").toInt(), course.value("required").toBool()});
    }
    return result;
}
}
