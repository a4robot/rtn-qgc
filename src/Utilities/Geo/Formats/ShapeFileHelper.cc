#include "ShapeFileHelper.h"
#ifdef QGC_ENABLE_QML
#include "KMLHelper.h"
#endif
#include "SHPFileHelper.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(ShapeFileHelperLog, "Utilities.ShapeFileHelper")

namespace {
    constexpr const char *_errorPrefix = QT_TRANSLATE_NOOP("ShapeFileHelper", "Shape file load failed. %1");
}

ShapeFileHelper::ShapeFileType ShapeFileHelper::_getShapeFileType(const QString &file, QString &errorString)
{
    errorString.clear();

    if (file.endsWith(kmlFileExtension, Qt::CaseInsensitive)) {
#ifdef QGC_ENABLE_QML
        return ShapeFileType::KML;
#else
        // KML plan-file support is QML-UI-only (wave 14 Q7c) — degrade to a
        // clean "unsupported" error rather than linking KMLHelper headless.
        errorString = tr(_errorPrefix).arg(tr("KML files are not supported in this build."));
        return ShapeFileType::None;
#endif
    } else if (file.endsWith(shpFileExtension, Qt::CaseInsensitive)) {
        return ShapeFileType::SHP;
    } else {
        // Strip leading dots for user-friendly error message
        const QString kmlExt = QString(kmlFileExtension).mid(1);
        const QString shpExt = QString(shpFileExtension).mid(1);
        errorString = tr(_errorPrefix).arg(tr("Unsupported file type. Only %1 and %2 are supported.").arg(kmlExt, shpExt));
    }

    return ShapeFileType::None;
}

ShapeFileHelper::ShapeType ShapeFileHelper::determineShapeType(const QString &file, QString &errorString)
{
    errorString.clear();

    switch (_getShapeFileType(file, errorString)) {
    case ShapeFileType::KML:
#ifdef QGC_ENABLE_QML
        return KMLHelper::determineShapeType(file, errorString);
#else
        return ShapeType::Error; // unreachable: _getShapeFileType never returns KML in this build
#endif
    case ShapeFileType::SHP:
        return SHPFileHelper::determineShapeType(file, errorString);
    case ShapeFileType::None:
    default:
        return ShapeType::Error;
    }
}

int ShapeFileHelper::getEntityCount(const QString &file, QString &errorString)
{
    errorString.clear();

    switch (_getShapeFileType(file, errorString)) {
    case ShapeFileType::KML:
#ifdef QGC_ENABLE_QML
        return KMLHelper::getEntityCount(file, errorString);
#else
        return 0; // unreachable: _getShapeFileType never returns KML in this build
#endif
    case ShapeFileType::SHP:
        return SHPFileHelper::getEntityCount(file, errorString);
    case ShapeFileType::None:
    default:
        return 0;
    }
}

bool ShapeFileHelper::loadPolygonsFromFile(const QString &file, QList<QList<QGeoCoordinate>> &polygons, QString &errorString, double filterMeters)
{
    errorString.clear();
    polygons.clear();

    switch (_getShapeFileType(file, errorString)) {
    case ShapeFileType::KML:
#ifdef QGC_ENABLE_QML
        return KMLHelper::loadPolygonsFromFile(file, polygons, errorString, filterMeters);
#else
        return false; // unreachable: _getShapeFileType never returns KML in this build
#endif
    case ShapeFileType::SHP:
        return SHPFileHelper::loadPolygonsFromFile(file, polygons, errorString, filterMeters);
    case ShapeFileType::None:
    default:
        return false;
    }
}

bool ShapeFileHelper::loadPolylinesFromFile(const QString &file, QList<QList<QGeoCoordinate>> &polylines, QString &errorString, double filterMeters)
{
    errorString.clear();
    polylines.clear();

    switch (_getShapeFileType(file, errorString)) {
    case ShapeFileType::KML:
#ifdef QGC_ENABLE_QML
        return KMLHelper::loadPolylinesFromFile(file, polylines, errorString, filterMeters);
#else
        return false; // unreachable: _getShapeFileType never returns KML in this build
#endif
    case ShapeFileType::SHP:
        return SHPFileHelper::loadPolylinesFromFile(file, polylines, errorString, filterMeters);
    case ShapeFileType::None:
    default:
        return false;
    }
}

bool ShapeFileHelper::loadPointsFromFile(const QString &file, QList<QGeoCoordinate> &points, QString &errorString)
{
    errorString.clear();
    points.clear();

    switch (_getShapeFileType(file, errorString)) {
    case ShapeFileType::KML:
#ifdef QGC_ENABLE_QML
        return KMLHelper::loadPointsFromFile(file, points, errorString);
#else
        return false; // unreachable: _getShapeFileType never returns KML in this build
#endif
    case ShapeFileType::SHP:
        return SHPFileHelper::loadPointsFromFile(file, points, errorString);
    case ShapeFileType::None:
    default:
        return false;
    }
}

QStringList ShapeFileHelper::fileDialogKMLFilters()
{
    static const QStringList filters = QStringList(tr("KML Files (*%1)").arg(kmlFileExtension));
    return filters;
}

QStringList ShapeFileHelper::fileDialogKMLOrSHPFilters()
{
    static const QStringList filters = QStringList(tr("KML/SHP Files (*%1 *%2)").arg(kmlFileExtension, shpFileExtension));
    return filters;
}
