#pragma once
#include <QList>
#include <QString>
namespace virgil {
struct CatalogCourse { QString code; QString name; int semester = 0; bool required = false; };
QList<CatalogCourse> courseCatalog();
}
