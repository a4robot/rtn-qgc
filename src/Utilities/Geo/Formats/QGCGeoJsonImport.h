#pragma once

#include <QtCore/QList>
#include <QtPositioning/QGeoPath>
#include <QtPositioning/QGeoPolygon>

class QJsonDocument;

/// Minimal RFC 7946 GeoJSON geometry importer, built only on QtPositioning types.
///
/// This covers exactly the subset GeoJsonHelper consumes: Point/LineString/Polygon and their
/// Multi* variants, wrapped in a bare Geometry, Feature, FeatureCollection, or GeometryCollection
/// document (arbitrarily nested). It intentionally does not attempt to be a general-purpose
/// GeoJSON library (no property/CRS handling) -- it exists so GeoJsonHelper does not have to link
/// against Qt6::Location just for QGeoJson::importGeoJson().
namespace QGCGeoJson
{
    enum class ShapeKind {
        Polygon,     ///< Polygon / MultiPolygon ring (outer ring only, holes are dropped)
        Polyline,    ///< LineString / MultiLineString
        Unsupported  ///< Recognized GeoJSON geometry type QGC does not surface as a shape (Point, MultiPoint)
    };

    struct Shape {
        ShapeKind kind = ShapeKind::Unsupported;
        QGeoPolygon polygon;
        QGeoPath path;
    };

    /// Recursively imports every geometry found in a GeoJSON document into a flat list of shapes.
    /// Malformed or unrecognized geometries are silently skipped rather than treated as a hard
    /// error, matching GeoJsonHelper's historical tolerance -- callers report "no shape found" /
    /// "no supported type found" based on the returned list instead.
    QList<Shape> importGeoJson(const QJsonDocument &jsonDoc);
}
