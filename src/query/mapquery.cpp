/*****************************************************************************
* Copyright 2015-2017 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "query/mapquery.h"

#include "common/constants.h"
#include "common/maptypesfactory.h"
#include "common/maptools.h"
#include "fs/common/binarygeometry.h"
#include "sql/sqlquery.h"
#include "query/airportquery.h"
#include "navapp.h"
#include "common/maptools.h"
#include "settings/settings.h"
#include "fs/common/xpgeometry.h"

#include <QDataStream>
#include <QRegularExpression>

using namespace Marble;
using namespace atools::sql;
using namespace atools::geo;
using map::MapAirport;
using map::MapVor;
using map::MapNdb;
using map::MapWaypoint;
using map::MapMarker;
using map::MapIls;
using map::MapParking;
using map::MapHelipad;

static double queryRectInflationFactor = 0.2;
static double queryRectInflationIncrement = 0.1;
int MapQuery::queryMaxRows = 5000;

struct MapAirspaceCoordinate
{
  atools::geo::Pos pos;
  float radius;
  QString type;
};

MapQuery::MapQuery(QObject *parent, atools::sql::SqlDatabase *sqlDb, SqlDatabase *sqlDbNav)
  : QObject(parent), db(sqlDb), dbNav(sqlDbNav)
{
  mapTypesFactory = new MapTypesFactory();
  atools::settings::Settings& settings = atools::settings::Settings::instance();

  runwayOverwiewCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_MAPQUERY + "RunwayOverwiewCache",
                                                           1000).toInt());
  airspaceLineCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_MAPQUERY + "AirspaceLineCache", 10000).toInt());

  queryRectInflationFactor = settings.getAndStoreValue(
    lnm::SETTINGS_MAPQUERY + "QueryRectInflationFactor", 0.3).toDouble();
  queryRectInflationIncrement = settings.getAndStoreValue(
    lnm::SETTINGS_MAPQUERY + "QueryRectInflationIncrement", 0.1).toDouble();
  queryMaxRows = settings.getAndStoreValue(
    lnm::SETTINGS_MAPQUERY + "QueryRowLimit", 5000).toInt();
}

MapQuery::~MapQuery()
{
  deInitQueries();
  delete mapTypesFactory;
}

map::MapAirport MapQuery::getAirportSim(const map::MapAirport& airport)
{
  if(airport.navdata)
  {
    map::MapAirport retval;
    NavApp::getAirportQuerySim()->getAirportByIdent(retval, airport.ident);
    return retval;
  }
  return airport;
}

map::MapAirport MapQuery::getAirportNav(const map::MapAirport& airport)
{
  if(!airport.navdata)
  {
    map::MapAirport retval;
    NavApp::getAirportQueryNav()->getAirportByIdent(retval, airport.ident);
    return retval;
  }
  return airport;
}

void MapQuery::getAirportSimReplace(map::MapAirport& airport)
{
  if(airport.navdata)
    NavApp::getAirportQuerySim()->getAirportByIdent(airport, airport.ident);
}

void MapQuery::getAirportNavReplace(map::MapAirport& airport)
{
  if(!airport.navdata)
    NavApp::getAirportQueryNav()->getAirportByIdent(airport, airport.ident);
}

void MapQuery::getVorForWaypoint(map::MapVor& vor, int waypointId)
{
  vorByWaypointIdQuery->bindValue(":id", waypointId);
  vorByWaypointIdQuery->exec();
  if(vorByWaypointIdQuery->next())
    mapTypesFactory->fillVor(vorByWaypointIdQuery->record(), vor);
  vorByWaypointIdQuery->finish();
}

void MapQuery::getNdbForWaypoint(map::MapNdb& ndb, int waypointId)
{
  ndbByWaypointIdQuery->bindValue(":id", waypointId);
  ndbByWaypointIdQuery->exec();
  if(ndbByWaypointIdQuery->next())
    mapTypesFactory->fillNdb(ndbByWaypointIdQuery->record(), ndb);
  ndbByWaypointIdQuery->finish();
}

void MapQuery::getVorNearest(map::MapVor& vor, const atools::geo::Pos& pos)
{
  vorNearestQuery->bindValue(":lonx", pos.getLonX());
  vorNearestQuery->bindValue(":laty", pos.getLatY());
  vorNearestQuery->exec();
  if(vorNearestQuery->next())
    mapTypesFactory->fillVor(vorNearestQuery->record(), vor);
  vorNearestQuery->finish();
}

void MapQuery::getNdbNearest(map::MapNdb& ndb, const atools::geo::Pos& pos)
{
  ndbNearestQuery->bindValue(":lonx", pos.getLonX());
  ndbNearestQuery->bindValue(":laty", pos.getLatY());
  ndbNearestQuery->exec();
  if(ndbNearestQuery->next())
    mapTypesFactory->fillNdb(ndbNearestQuery->record(), ndb);
  ndbNearestQuery->finish();
}

void MapQuery::getAirwaysForWaypoint(QList<map::MapAirway>& airways, int waypointId)
{
  airwayByWaypointIdQuery->bindValue(":id", waypointId);
  airwayByWaypointIdQuery->exec();
  while(airwayByWaypointIdQuery->next())
  {
    map::MapAirway airway;
    mapTypesFactory->fillAirway(airwayByWaypointIdQuery->record(), airway);
    airways.append(airway);
  }
}

void MapQuery::getWaypointsForAirway(QList<map::MapWaypoint>& waypoints, const QString& airwayName,
                                     const QString& waypointIdent)
{
  airwayWaypointByIdentQuery->bindValue(":waypoint", waypointIdent.isEmpty() ? "%" : waypointIdent);
  airwayWaypointByIdentQuery->bindValue(":airway", airwayName.isEmpty() ? "%" : airwayName);
  airwayWaypointByIdentQuery->exec();
  while(airwayWaypointByIdentQuery->next())
  {
    map::MapWaypoint waypoint;
    mapTypesFactory->fillWaypoint(airwayWaypointByIdentQuery->record(), waypoint);
    waypoints.append(waypoint);
  }
}

void MapQuery::getWaypointListForAirwayName(QList<map::MapAirwayWaypoint>& waypoints,
                                            const QString& airwayName)
{
  airwayWaypointsQuery->bindValue(":name", airwayName);
  airwayWaypointsQuery->exec();

  // Collect records first
  SqlRecordVector records;
  while(airwayWaypointsQuery->next())
    records.append(airwayWaypointsQuery->record());

  for(int i = 0; i < records.size(); i++)
  {
    const SqlRecord& rec = records.at(i);

    int fragment = rec.valueInt("airway_fragment_no");
    // Check if the next fragment is different
    int nextFragment = i < records.size() - 1 ?
                       records.at(i + 1).valueInt("airway_fragment_no") : -1;

    map::MapAirwayWaypoint aw;
    aw.airwayFragmentId = fragment;
    aw.seqNum = rec.valueInt("sequence_no");
    aw.airwayId = rec.valueInt("airway_id");

    // Add from waypoint
    map::MapSearchResult result;
    int fromId = rec.valueInt("from_waypoint_id");
    getMapObjectById(result, map::WAYPOINT, fromId, false /* airport from nav database */);
    if(!result.waypoints.isEmpty())
      aw.waypoint = result.waypoints.first();
    else
      qWarning() << "getWaypointListForAirwayName: no waypoint for" << airwayName << "wp id" << fromId;
    waypoints.append(aw);

    if(i == records.size() - 1 || fragment != nextFragment)
    {
      // Add to waypoint if this is the last one or if the fragment is about to change
      result.waypoints.clear();
      int toId = rec.valueInt("to_waypoint_id");
      getMapObjectById(result, map::WAYPOINT, toId, false /* airport from nav database */);
      if(!result.waypoints.isEmpty())
        aw.waypoint = result.waypoints.first();
      else
        qWarning() << "getWaypointListForAirwayName: no waypoint for" << airwayName << "wp id" << toId;
      waypoints.append(aw);
    }
  }
}

void MapQuery::getAirwayById(map::MapAirway& airway, int airwayId)
{
  airwayByIdQuery->bindValue(":id", airwayId);
  airwayByIdQuery->exec();
  if(airwayByIdQuery->next())
    mapTypesFactory->fillAirway(airwayByIdQuery->record(), airway);
  airwayByIdQuery->finish();

}

void MapQuery::getAirwayByNameAndWaypoint(map::MapAirway& airway, const QString& airwayName, const QString& waypoint1,
                                          const QString& waypoint2)
{
  if(airwayName.isEmpty() || waypoint1.isEmpty() || waypoint2.isEmpty())
    return;

  airwayByNameAndWaypointQuery->bindValue(":airway", airwayName);
  airwayByNameAndWaypointQuery->bindValue(":ident1", waypoint1);
  airwayByNameAndWaypointQuery->bindValue(":ident2", waypoint2);
  airwayByNameAndWaypointQuery->exec();
  if(airwayByNameAndWaypointQuery->next())
    mapTypesFactory->fillAirway(airwayByNameAndWaypointQuery->record(), airway);
  airwayByNameAndWaypointQuery->finish();
}

map::MapAirspace MapQuery::getAirspaceById(int airspaceId)
{
  map::MapAirspace airspace;
  getAirspaceById(airspace, airspaceId);
  return airspace;
}

void MapQuery::getAirspaceById(map::MapAirspace& airspace, int airspaceId)
{
  airspaceByIdQuery->bindValue(":id", airspaceId);
  airspaceByIdQuery->exec();
  if(airspaceByIdQuery->next())
    mapTypesFactory->fillAirspace(airspaceByIdQuery->record(), airspace);
  airspaceByIdQuery->finish();
}

void MapQuery::getMapObjectByIdent(map::MapSearchResult& result, map::MapObjectTypes type,
                                   const QString& ident, const QString& region, const QString& airport,
                                   const Pos& sortByDistancePos, float maxDistance, bool airportFromNavDatabase)
{
  mapObjectByIdentInternal(result, type, ident, region, airport, sortByDistancePos, maxDistance,
                           airportFromNavDatabase);
}

void MapQuery::getMapObjectByIdent(map::MapSearchResult& result, map::MapObjectTypes type, const QString& ident,
                                   const QString& region, const QString& airport, bool airportFromNavDatabase)
{
  mapObjectByIdentInternal(result, type, ident, region, airport, EMPTY_POS, map::INVALID_DISTANCE_VALUE,
                           airportFromNavDatabase);
}

void MapQuery::mapObjectByIdentInternal(map::MapSearchResult& result, map::MapObjectTypes type, const QString& ident,
                                        const QString& region, const QString& airport, const Pos& sortByDistancePos,
                                        float maxDistance, bool airportFromNavDatabase)
{
  if(type & map::AIRPORT)
  {
    map::MapAirport ap;

    if(airportFromNavDatabase)
      NavApp::getAirportQueryNav()->getAirportByIdent(ap, ident);
    else
      NavApp::getAirportQuerySim()->getAirportByIdent(ap, ident);

    if(ap.isValid())
    {
      result.airports.append(ap);
      maptools::sortByDistance(result.airports, sortByDistancePos);
      maptools::removeByDistance(result.airports, sortByDistancePos, maxDistance);
    }
  }

  if(type & map::VOR)
  {
    vorByIdentQuery->bindValue(":ident", ident);
    vorByIdentQuery->bindValue(":region", region.isEmpty() ? "%" : region);
    vorByIdentQuery->exec();
    while(vorByIdentQuery->next())
    {
      map::MapVor vor;
      mapTypesFactory->fillVor(vorByIdentQuery->record(), vor);
      result.vors.append(vor);
    }
    maptools::sortByDistance(result.vors, sortByDistancePos);
    maptools::removeByDistance(result.vors, sortByDistancePos, maxDistance);
  }

  if(type & map::NDB)
  {
    ndbByIdentQuery->bindValue(":ident", ident);
    ndbByIdentQuery->bindValue(":region", region.isEmpty() ? "%" : region);
    ndbByIdentQuery->exec();
    while(ndbByIdentQuery->next())
    {
      map::MapNdb ndb;
      mapTypesFactory->fillNdb(ndbByIdentQuery->record(), ndb);
      result.ndbs.append(ndb);
    }
    maptools::sortByDistance(result.ndbs, sortByDistancePos);
    maptools::removeByDistance(result.ndbs, sortByDistancePos, maxDistance);
  }

  if(type & map::WAYPOINT)
  {
    waypointByIdentQuery->bindValue(":ident", ident);
    waypointByIdentQuery->bindValue(":region", region.isEmpty() ? "%" : region);
    waypointByIdentQuery->exec();
    while(waypointByIdentQuery->next())
    {
      map::MapWaypoint wp;
      mapTypesFactory->fillWaypoint(waypointByIdentQuery->record(), wp);
      result.waypoints.append(wp);
    }
    maptools::sortByDistance(result.waypoints, sortByDistancePos);
    maptools::removeByDistance(result.waypoints, sortByDistancePos, maxDistance);
  }

  if(type & map::ILS)
  {
    ilsByIdentQuery->bindValue(":ident", ident);
    ilsByIdentQuery->bindValue(":airport", airport);
    ilsByIdentQuery->exec();
    while(ilsByIdentQuery->next())
    {
      map::MapIls ils;
      mapTypesFactory->fillIls(ilsByIdentQuery->record(), ils);
      result.ils.append(ils);
    }
    maptools::sortByDistance(result.ils, sortByDistancePos);
    maptools::removeByDistance(result.ils, sortByDistancePos, maxDistance);
  }

  if(type & map::RUNWAYEND)
  {
    if(airportFromNavDatabase)
      NavApp::getAirportQueryNav()->getRunwayEndByNames(result, ident, airport);
    else
      NavApp::getAirportQuerySim()->getRunwayEndByNames(result, ident, airport);
  }

  if(type & map::AIRWAY)
  {
    airwayByNameQuery->bindValue(":name", ident);
    airwayByNameQuery->exec();
    while(airwayByNameQuery->next())
    {
      map::MapAirway airway;
      mapTypesFactory->fillAirway(airwayByNameQuery->record(), airway);
      result.airways.append(airway);
    }
  }
}

void MapQuery::getMapObjectById(map::MapSearchResult& result, map::MapObjectTypes type, int id,
                                bool airportFromNavDatabase)
{
  if(type == map::AIRPORT)
  {
    map::MapAirport airport = (airportFromNavDatabase ?
                               NavApp::getAirportQueryNav() :
                               NavApp::getAirportQuerySim())->getAirportById(id);
    if(airport.isValid())
      result.airports.append(airport);
  }
  else if(type == map::VOR)
  {
    map::MapVor vor = getVorById(id);
    if(vor.isValid())
      result.vors.append(vor);
  }
  else if(type == map::NDB)
  {
    map::MapNdb ndb = getNdbById(id);
    if(ndb.isValid())
      result.ndbs.append(ndb);
  }
  else if(type == map::WAYPOINT)
  {
    map::MapWaypoint waypoint = getWaypointById(id);
    if(waypoint.isValid())
      result.waypoints.append(waypoint);
  }
  else if(type == map::ILS)
  {
    map::MapIls ils = getIlsById(id);
    if(ils.isValid())
      result.ils.append(ils);
  }
  else if(type == map::RUNWAYEND)
  {
    map::MapRunwayEnd end = (airportFromNavDatabase ?
                             NavApp::getAirportQueryNav() :
                             NavApp::getAirportQuerySim())->getRunwayEndById(id);
    if(end.isValid())
      result.runwayEnds.append(end);
  }
  else if(type == map::AIRSPACE)
  {
    map::MapAirspace airspace = getAirspaceById(id);
    if(airspace.isValid())
      result.airspaces.append(airspace);
  }
}

map::MapVor MapQuery::getVorById(int id)
{
  map::MapVor vor;
  vorByIdQuery->bindValue(":id", id);
  vorByIdQuery->exec();
  if(vorByIdQuery->next())
    mapTypesFactory->fillVor(vorByIdQuery->record(), vor);
  vorByIdQuery->finish();
  return vor;
}

map::MapNdb MapQuery::getNdbById(int id)
{
  map::MapNdb ndb;
  ndbByIdQuery->bindValue(":id", id);
  ndbByIdQuery->exec();
  if(ndbByIdQuery->next())
    mapTypesFactory->fillNdb(ndbByIdQuery->record(), ndb);
  ndbByIdQuery->finish();
  return ndb;
}

map::MapIls MapQuery::getIlsById(int id)
{
  map::MapIls ils;
  ilsByIdQuery->bindValue(":id", id);
  ilsByIdQuery->exec();
  if(ilsByIdQuery->next())
    mapTypesFactory->fillIls(ilsByIdQuery->record(), ils);
  ilsByIdQuery->finish();
  return ils;
}

map::MapWaypoint MapQuery::getWaypointById(int id)
{
  map::MapWaypoint wp;
  waypointByIdQuery->bindValue(":id", id);
  waypointByIdQuery->exec();
  if(waypointByIdQuery->next())
    mapTypesFactory->fillWaypoint(waypointByIdQuery->record(), wp);
  waypointByIdQuery->finish();
  return wp;
}

void MapQuery::getNearestObjects(const CoordinateConverter& conv, const MapLayer *mapLayer,
                                 bool airportDiagram, map::MapObjectTypes types,
                                 int xs, int ys, int screenDistance,
                                 map::MapSearchResult& result)
{
  using maptools::insertSortedByDistance;
  using maptools::insertSortedByTowerDistance;

  int x, y;
  if(mapLayer->isAirport() && types.testFlag(map::AIRPORT))
  {
    for(int i = airportCache.list.size() - 1; i >= 0; i--)
    {
      const MapAirport& airport = airportCache.list.at(i);

      if(airport.isVisible(types))
      {
        if(conv.wToS(airport.position, x, y))
          if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
            insertSortedByDistance(conv, result.airports, &result.airportIds, xs, ys, airport);

        if(airportDiagram)
        {
          // Include tower for airport diagrams
          if(conv.wToS(airport.towerCoords, x, y) &&
             atools::geo::manhattanDistance(x, y, xs, ys) < screenDistance)
            insertSortedByTowerDistance(conv, result.towers, xs, ys, airport);
        }
      }
    }
  }

  if(mapLayer->isVor() && types.testFlag(map::VOR))
  {
    for(int i = vorCache.list.size() - 1; i >= 0; i--)
    {
      const MapVor& vor = vorCache.list.at(i);
      if(conv.wToS(vor.position, x, y))
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.vors, &result.vorIds, xs, ys, vor);
    }
  }

  if(mapLayer->isNdb() && types.testFlag(map::NDB))
  {
    for(int i = ndbCache.list.size() - 1; i >= 0; i--)
    {
      const MapNdb& ndb = ndbCache.list.at(i);
      if(conv.wToS(ndb.position, x, y))
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.ndbs, &result.ndbIds, xs, ys, ndb);
    }
  }

  if(mapLayer->isWaypoint() && types.testFlag(map::WAYPOINT))
  {
    for(int i = waypointCache.list.size() - 1; i >= 0; i--)
    {
      const MapWaypoint& wp = waypointCache.list.at(i);
      if(conv.wToS(wp.position, x, y))
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.waypoints, &result.waypointIds, xs, ys, wp);
    }
  }

  if(mapLayer->isAirwayWaypoint())
  {
    for(int i = waypointCache.list.size() - 1; i >= 0; i--)
    {
      const MapWaypoint& wp = waypointCache.list.at(i);
      if((wp.hasVictorAirways && types.testFlag(map::AIRWAYV)) ||
         (wp.hasJetAirways && types.testFlag(map::AIRWAYJ)))
        if(conv.wToS(wp.position, x, y))
          if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
            insertSortedByDistance(conv, result.waypoints, &result.waypointIds, xs, ys, wp);
    }
  }

  if(mapLayer->isMarker() && types.testFlag(map::MARKER))
  {
    for(int i = markerCache.list.size() - 1; i >= 0; i--)
    {
      const MapMarker& wp = markerCache.list.at(i);
      if(conv.wToS(wp.position, x, y))
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.markers, nullptr, xs, ys, wp);
    }
  }

  if(mapLayer->isIls() && types.testFlag(map::ILS))
  {
    for(int i = ilsCache.list.size() - 1; i >= 0; i--)
    {
      const MapIls& wp = ilsCache.list.at(i);
      if(conv.wToS(wp.position, x, y))
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.ils, nullptr, xs, ys, wp);
    }
  }

  if(mapLayer->isAirport() && types.testFlag(map::AIRPORT))
  {
    if(airportDiagram)
    {
      QHash<int, QList<map::MapParking> > parkingCache = NavApp::getAirportQuerySim()->getParkingCache();

      // Also check parking and helipads in airport diagrams
      for(int id : parkingCache.keys())
      {
        const QList<MapParking>& parkings = parkingCache.value(id);
        for(const MapParking& p : parkings)
        {
          if(conv.wToS(p.position, x, y) && atools::geo::manhattanDistance(x, y, xs, ys) < screenDistance)
            insertSortedByDistance(conv, result.parkings, nullptr, xs, ys, p);
        }
      }

      QHash<int, QList<map::MapHelipad> > helipadCache = NavApp::getAirportQuerySim()->getHelipadCache();

      for(int id : helipadCache.keys())
      {
        const QList<MapHelipad>& helipads = helipadCache.value(id);
        for(const MapHelipad& p : helipads)
        {
          if(conv.wToS(p.position, x, y) && atools::geo::manhattanDistance(x, y, xs, ys) < screenDistance)
            insertSortedByDistance(conv, result.helipads, nullptr, xs, ys, p);
        }
      }
    }
  }
}

const QList<map::MapAirport> *MapQuery::getAirports(const Marble::GeoDataLatLonBox& rect,
                                                    const MapLayer *mapLayer, bool lazy)
{
  airportCache.updateCache(rect, mapLayer, lazy,
                           [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersAirport(newLayer);
  });

  switch(mapLayer->getDataSource())
  {
    case layer::ALL:
      airportByRectQuery->bindValue(":minlength", mapLayer->getMinRunwayLength());
      return fetchAirports(rect, airportByRectQuery, true /* reverse */, lazy, false /* overview */);

    case layer::MEDIUM:
      // Airports > 4000 ft
      return fetchAirports(rect, airportMediumByRectQuery, false /* reverse */, lazy, true /* overview */);

    case layer::LARGE:
      // Airports > 8000 ft
      return fetchAirports(rect, airportLargeByRectQuery, false /* reverse */, lazy, true /* overview */);

  }
  return nullptr;
}

const QList<map::MapWaypoint> *MapQuery::getWaypoints(const GeoDataLatLonBox& rect,
                                                      const MapLayer *mapLayer, bool lazy)
{
  waypointCache.updateCache(rect, mapLayer, lazy,
                            [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersWaypoint(newLayer);
  });

  if(waypointCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, waypointsByRectQuery);
      waypointsByRectQuery->exec();
      while(waypointsByRectQuery->next())
      {
        map::MapWaypoint wp;
        mapTypesFactory->fillWaypoint(waypointsByRectQuery->record(), wp);
        waypointCache.list.append(wp);
      }
    }
  }
  waypointCache.validate();
  return &waypointCache.list;
}

const QList<map::MapVor> *MapQuery::getVors(const GeoDataLatLonBox& rect, const MapLayer *mapLayer,
                                            bool lazy)
{
  vorCache.updateCache(rect, mapLayer, lazy,
                       [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersVor(newLayer);
  });

  if(vorCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, vorsByRectQuery);
      vorsByRectQuery->exec();
      while(vorsByRectQuery->next())
      {
        map::MapVor vor;
        mapTypesFactory->fillVor(vorsByRectQuery->record(), vor);
        vorCache.list.append(vor);
      }
    }
  }
  vorCache.validate();
  return &vorCache.list;
}

const QList<map::MapNdb> *MapQuery::getNdbs(const GeoDataLatLonBox& rect, const MapLayer *mapLayer,
                                            bool lazy)
{
  ndbCache.updateCache(rect, mapLayer, lazy,
                       [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersNdb(newLayer);
  });

  if(ndbCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, ndbsByRectQuery);
      ndbsByRectQuery->exec();
      while(ndbsByRectQuery->next())
      {
        map::MapNdb ndb;
        mapTypesFactory->fillNdb(ndbsByRectQuery->record(), ndb);
        ndbCache.list.append(ndb);
      }
    }
  }
  ndbCache.validate();
  return &ndbCache.list;
}

const QList<map::MapMarker> *MapQuery::getMarkers(const GeoDataLatLonBox& rect, const MapLayer *mapLayer,
                                                  bool lazy)
{
  markerCache.updateCache(rect, mapLayer, lazy,
                          [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersMarker(newLayer);
  });

  if(markerCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, markersByRectQuery);
      markersByRectQuery->exec();
      while(markersByRectQuery->next())
      {
        map::MapMarker marker;
        mapTypesFactory->fillMarker(markersByRectQuery->record(), marker);
        markerCache.list.append(marker);
      }
    }
  }
  markerCache.validate();
  return &markerCache.list;
}

const QList<map::MapIls> *MapQuery::getIls(const GeoDataLatLonBox& rect, const MapLayer *mapLayer, bool lazy)
{
  ilsCache.updateCache(rect, mapLayer, lazy,
                       [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersIls(newLayer);
  });

  if(ilsCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, ilsByRectQuery);
      ilsByRectQuery->exec();
      while(ilsByRectQuery->next())
      {
        map::MapIls ils;
        mapTypesFactory->fillIls(ilsByRectQuery->record(), ils);
        ilsCache.list.append(ils);
      }
    }
  }
  ilsCache.validate();
  return &ilsCache.list;
}

const QList<map::MapAirway> *MapQuery::getAirways(const GeoDataLatLonBox& rect, const MapLayer *mapLayer, bool lazy)
{
  airwayCache.updateCache(rect, mapLayer, lazy,
                          [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersAirway(newLayer);
  });

  if(airwayCache.list.isEmpty() && !lazy)
  {
    QSet<int> ids;
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, airwayByRectQuery);
      airwayByRectQuery->exec();
      while(airwayByRectQuery->next())
      {
        if(ids.contains(airwayByRectQuery->valueInt("airway_id")))
          continue;

        // qreal north, qreal south, qreal east, qreal west
        if(rect.intersects(GeoDataLatLonBox(airwayByRectQuery->valueFloat("top_laty"),
                                            airwayByRectQuery->valueFloat("bottom_laty"),
                                            airwayByRectQuery->valueFloat("right_lonx"),
                                            airwayByRectQuery->valueFloat("left_lonx"),
                                            GeoDataCoordinates::GeoDataCoordinates::Degree)))
        {
          map::MapAirway airway;
          mapTypesFactory->fillAirway(airwayByRectQuery->record(), airway);
          airwayCache.list.append(airway);
          ids.insert(airway.id);
        }
      }
    }
  }
  airwayCache.validate();
  return &airwayCache.list;
}

const QList<map::MapAirspace> *MapQuery::getAirspaces(const GeoDataLatLonBox& rect, const MapLayer *mapLayer,
                                                      map::MapAirspaceFilter filter, float flightPlanAltitude,
                                                      bool lazy)
{
  airspaceCache.updateCache(rect, mapLayer, lazy,
                            [](const MapLayer *curLayer, const MapLayer *newLayer) -> bool
  {
    return curLayer->hasSameQueryParametersAirspace(newLayer);
  });

  if(filter.types != lastAirspaceFilter.types || filter.flags != lastAirspaceFilter.flags ||
     atools::almostNotEqual(lastFlightplanAltitude, flightPlanAltitude))
  {
    // Need a few more parameters to clear the cache which is different to other map features
    airspaceCache.list.clear();
    lastAirspaceFilter = filter;
    lastFlightplanAltitude = flightPlanAltitude;
  }

  if(airspaceCache.list.isEmpty() && !lazy)
  {
    QStringList typeStrings;

    if(filter.types != map::AIRSPACE_NONE)
    {
      // Build a list of query strings based on the bitfield
      if(filter.types == map::AIRSPACE_ALL)
        typeStrings.append("%");
      else
      {
        for(int i = 0; i <= map::MAP_AIRSPACE_TYPE_BITS; i++)
        {
          map::MapAirspaceTypes t(1 << i);
          if(filter.types & t)
            typeStrings.append(map::airspaceTypeToDatabase(t));
        }
      }

      SqlQuery *query = nullptr;
      int alt;
      if(filter.flags & map::AIRSPACE_AT_FLIGHTPLAN)
      {
        query = airspaceByRectAtAltQuery;
        alt = atools::roundToInt(flightPlanAltitude);
      }
      else if(filter.flags & map::AIRSPACE_BELOW_10000)
      {
        query = airspaceByRectBelowAltQuery;
        alt = 10000;
      }
      else if(filter.flags & map::AIRSPACE_BELOW_18000)
      {
        query = airspaceByRectBelowAltQuery;
        alt = 18000;
      }
      else if(filter.flags & map::AIRSPACE_ABOVE_10000)
      {
        query = airspaceByRectAboveAltQuery;
        alt = 10000;
      }
      else if(filter.flags & map::AIRSPACE_ABOVE_18000)
      {
        query = airspaceByRectAboveAltQuery;
        alt = 18000;
      }
      else
      {
        query = airspaceByRectQuery;
        alt = 0;
      }

      QSet<int> ids;

      // qDebug() << rect.toString(GeoDataCoordinates::Degree);

      // Get the airspace objects without geometry
      for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
      {
        // qDebug() << r.toString(GeoDataCoordinates::Degree);

        for(const QString& typeStr : typeStrings)
        {
          bindCoordinatePointInRect(r, query);
          query->bindValue(":type", typeStr);

          if(alt > 0)
            query->bindValue(":alt", alt);

          // qDebug() << "==================== query" << endl << query->getFullQueryString();

          query->exec();
          while(query->next())
          {
            // Avoid double airspaces which can happen if they cross the date boundary
            if(ids.contains(query->valueInt("boundary_id")))
              continue;

            // qreal north, qreal south, qreal east, qreal west
            if(rect.intersects(GeoDataLatLonBox(query->valueFloat("max_laty"), query->valueFloat("min_laty"),
                                                query->valueFloat("max_lonx"), query->valueFloat("min_lonx"),
                                                GeoDataCoordinates::GeoDataCoordinates::Degree)))
            {
              map::MapAirspace airspace;
              mapTypesFactory->fillAirspace(query->record(), airspace);
              airspaceCache.list.append(airspace);

              ids.insert(airspace.id);
            }
          }
        }
      }

      // Sort by importance
      std::sort(airspaceCache.list.begin(), airspaceCache.list.end(),
                [](const map::MapAirspace& airspace1, const map::MapAirspace& airspace2) -> bool
      {
        return map::airspaceDrawingOrder(airspace1.type) < map::airspaceDrawingOrder(airspace2.type);
      });
    }
  }
  airspaceCache.validate();
  return &airspaceCache.list;
}

const LineString *MapQuery::getAirspaceGeometry(int boundaryId)
{
  if(airspaceLineCache.contains(boundaryId))
    return airspaceLineCache.object(boundaryId);
  else
  {
    LineString *lines = new LineString;

    airspaceLinesByIdQuery->bindValue(":id", boundaryId);
    airspaceLinesByIdQuery->exec();
    if(airspaceLinesByIdQuery->next())
    {
      atools::fs::common::BinaryGeometry geometry(airspaceLinesByIdQuery->value("geometry").toByteArray());
      geometry.swapGeometry(*lines);

      // qDebug() << *lines;
    }

    airspaceLineCache.insert(boundaryId, lines);

    return lines;
  }
}

/*
 * Get airport cache
 * @param reverse reverse order of airports to have unimportant small ones below in painting order
 * @param lazy do not update cache - instead return incomplete resut
 * @param overview fetch only incomplete data for overview airports
 * @return pointer to the airport cache
 */
const QList<map::MapAirport> *MapQuery::fetchAirports(const Marble::GeoDataLatLonBox& rect,
                                                      atools::sql::SqlQuery *query, bool reverse,
                                                      bool lazy, bool overview)
{
  if(airportCache.list.isEmpty() && !lazy)
  {
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinatePointInRect(r, query);
      query->exec();
      while(query->next())
      {
        map::MapAirport ap;
        if(overview)
          // Fill only a part of the object
          mapTypesFactory->fillAirportForOverview(query->record(), ap);
        else
          mapTypesFactory->fillAirport(query->record(), ap, true /* complete */, false /* nav */);

        if(reverse)
          airportCache.list.prepend(ap);
        else
          airportCache.list.append(ap);
      }
    }
  }
  airportCache.validate();
  return &airportCache.list;
}

const QList<map::MapRunway> *MapQuery::getRunwaysForOverview(int airportId)
{
  if(runwayOverwiewCache.contains(airportId))
    return runwayOverwiewCache.object(airportId);
  else
  {
    using atools::geo::Pos;

    runwayOverviewQuery->bindValue(":airportId", airportId);
    runwayOverviewQuery->exec();

    QList<map::MapRunway> *rws = new QList<map::MapRunway>;
    while(runwayOverviewQuery->next())
    {
      map::MapRunway runway;
      mapTypesFactory->fillRunway(runwayOverviewQuery->record(), runway, true);
      rws->append(runway);
    }
    runwayOverwiewCache.insert(airportId, rws);
    return rws;
  }
}

/*
 * Bind rectangle coordinates to a query.
 * @param rect
 * @param query
 * @param prefix used to prefix each bind variable
 */
void MapQuery::bindCoordinatePointInRect(const Marble::GeoDataLatLonBox& rect, atools::sql::SqlQuery *query,
                                         const QString& prefix)
{
  query->bindValue(":" + prefix + "leftx", rect.west(GeoDataCoordinates::Degree));
  query->bindValue(":" + prefix + "rightx", rect.east(GeoDataCoordinates::Degree));
  query->bindValue(":" + prefix + "bottomy", rect.south(GeoDataCoordinates::Degree));
  query->bindValue(":" + prefix + "topy", rect.north(GeoDataCoordinates::Degree));
}

/* Inflates the rectangle and splits it at the antimeridian (date line) if it overlaps */
QList<Marble::GeoDataLatLonBox> MapQuery::splitAtAntiMeridian(const Marble::GeoDataLatLonBox& rect)
{
  GeoDataLatLonBox newRect = rect;
  inflateRect(newRect);

  if(newRect.crossesDateLine())
  {
    // Split in western and eastern part
    GeoDataLatLonBox westOf;
    westOf.setBoundaries(newRect.north(GeoDataCoordinates::Degree),
                         newRect.south(GeoDataCoordinates::Degree),
                         180.,
                         newRect.west(GeoDataCoordinates::Degree), GeoDataCoordinates::Degree);

    GeoDataLatLonBox eastOf;
    eastOf.setBoundaries(newRect.north(GeoDataCoordinates::Degree),
                         newRect.south(GeoDataCoordinates::Degree),
                         newRect.east(GeoDataCoordinates::Degree),
                         -180., GeoDataCoordinates::Degree);

    return QList<GeoDataLatLonBox>({westOf, eastOf});
  }
  else
    return QList<GeoDataLatLonBox>({newRect});
}

/* Inflate rect by width and height in degrees. If it crosses the poles or date line it will be limited */
void MapQuery::inflateRect(Marble::GeoDataLatLonBox& rect)
{
  rect.scale(1. + queryRectInflationFactor, 1. + queryRectInflationFactor);

  if(rect.east(GeoDataCoordinates::Degree) + queryRectInflationIncrement < 180.f)
    rect.setEast(rect.east(GeoDataCoordinates::Degree) + queryRectInflationIncrement, GeoDataCoordinates::Degree);

  if(rect.west(GeoDataCoordinates::Degree) - queryRectInflationIncrement > -180.f)
    rect.setWest(rect.west(GeoDataCoordinates::Degree) - queryRectInflationIncrement, GeoDataCoordinates::Degree);

  if(rect.north(GeoDataCoordinates::Degree) + queryRectInflationIncrement < 90.f)
    rect.setNorth(rect.north(GeoDataCoordinates::Degree) + queryRectInflationIncrement, GeoDataCoordinates::Degree);
  if(rect.south(GeoDataCoordinates::Degree) - queryRectInflationIncrement > -90.f)
    rect.setSouth(rect.south(GeoDataCoordinates::Degree) - queryRectInflationIncrement, GeoDataCoordinates::Degree);

  // qDebug() << rect.toString(GeoDataCoordinates::Degree);
}

void MapQuery::initQueries()
{
  // Common where clauses
  static const QString whereRect("lonx between :leftx and :rightx and laty between :bottomy and :topy");
  static const QString whereIdentRegion("ident = :ident and region like :region");
  static const QString whereLimit("limit " + QString::number(queryMaxRows));

  // Common select statements
  QStringList const airportQueryBase = AirportQuery::airportColumns(db);

  static const QString airportQueryBaseOverview(
    "airport_id, ident, name, "
    "has_avgas, has_jetfuel, "
    "tower_frequency, "
    "is_closed, is_military, is_addon, rating, "
    "num_runway_hard, num_runway_soft, num_runway_water, num_helipad, "
    "longest_runway_length, longest_runway_heading, mag_var, "
    "lonx, laty, left_lonx, top_laty, right_lonx, bottom_laty ");

  static const QString airwayQueryBase(
    "airway_id, airway_name, airway_type, airway_fragment_no, sequence_no, from_waypoint_id, to_waypoint_id, "
    "direction, minimum_altitude, maximum_altitude, from_lonx, from_laty, to_lonx, to_laty ");

  static const QString airspaceQueryBase(
    "boundary_id, type, name, com_type, com_frequency, com_name, "
    "min_altitude_type, max_altitude_type, max_altitude, max_lonx, max_laty, min_altitude, min_lonx, min_laty ");

  static const QString waypointQueryBase(
    "waypoint_id, ident, region, type, num_victor_airway, num_jet_airway, "
    "mag_var, lonx, laty ");

  static const QString vorQueryBase(
    "vor_id, ident, name, region, type, name, frequency, channel, range, dme_only, dme_altitude, "
    "mag_var, altitude, lonx, laty ");
  static const QString ndbQueryBase(
    "ndb_id, ident, name, region, type, name, frequency, range, mag_var, altitude, lonx, laty ");

  static const QString parkingQueryBase(
    "parking_id, airport_id, type, name, airline_codes, number, radius, heading, has_jetway, lonx, laty ");

  static const QString ilsQueryBase(
    "ils_id, ident, name, region, mag_var, loc_heading, gs_pitch, frequency, range, dme_range, loc_width, "
    "end1_lonx, end1_laty, end_mid_lonx, end_mid_laty, end2_lonx, end2_laty, altitude, lonx, laty");

  deInitQueries();

  vorByIdentQuery = new SqlQuery(dbNav);
  vorByIdentQuery->prepare("select " + vorQueryBase + " from vor where " + whereIdentRegion);

  ndbByIdentQuery = new SqlQuery(dbNav);
  ndbByIdentQuery->prepare("select " + ndbQueryBase + " from ndb where " + whereIdentRegion);

  waypointByIdentQuery = new SqlQuery(dbNav);
  waypointByIdentQuery->prepare("select " + waypointQueryBase + " from waypoint where " + whereIdentRegion);

  ilsByIdentQuery = new SqlQuery(db);
  ilsByIdentQuery->prepare("select " + ilsQueryBase +
                           " from ils where ident = :ident and loc_airport_ident = :airport");

  vorByIdQuery = new SqlQuery(dbNav);
  vorByIdQuery->prepare("select " + vorQueryBase + " from vor where vor_id = :id");

  ndbByIdQuery = new SqlQuery(dbNav);
  ndbByIdQuery->prepare("select " + ndbQueryBase + " from ndb where ndb_id = :id");

  // Get VOR for waypoint
  vorByWaypointIdQuery = new SqlQuery(dbNav);
  vorByWaypointIdQuery->prepare("select " + vorQueryBase +
                                " from vor where vor_id in "
                                "(select nav_id from waypoint w where w.waypoint_id = :id)");

  // Get NDB for waypoint
  ndbByWaypointIdQuery = new SqlQuery(dbNav);
  ndbByWaypointIdQuery->prepare("select " + ndbQueryBase +
                                " from ndb where ndb_id in "
                                "(select nav_id from waypoint w where w.waypoint_id = :id)");

  // Get nearest VOR
  vorNearestQuery = new SqlQuery(dbNav);
  vorNearestQuery->prepare(
    "select " + vorQueryBase + " from vor order by (abs(lonx - :lonx) + abs(laty - :laty)) limit 1");

  // Get nearest NDB
  ndbNearestQuery = new SqlQuery(dbNav);
  ndbNearestQuery->prepare(
    "select " + ndbQueryBase + " from ndb order by (abs(lonx - :lonx) + abs(laty - :laty)) limit 1");

  waypointByIdQuery = new SqlQuery(dbNav);
  waypointByIdQuery->prepare("select " + waypointQueryBase + " from waypoint where waypoint_id = :id");

  ilsByIdQuery = new SqlQuery(db);
  ilsByIdQuery->prepare("select " + ilsQueryBase + " from ils where ils_id = :id");

  airportByRectQuery = new SqlQuery(db);
  airportByRectQuery->prepare(
    "select " + airportQueryBase.join(", ") + " from airport where " + whereRect +
    " and longest_runway_length >= :minlength order by rating desc, longest_runway_length desc "
    + whereLimit);

  airportMediumByRectQuery = new SqlQuery(db);
  airportMediumByRectQuery->prepare(
    "select " + airportQueryBaseOverview + "from airport_medium where " + whereRect + " " + whereLimit);

  airportLargeByRectQuery = new SqlQuery(db);
  airportLargeByRectQuery->prepare(
    "select " + airportQueryBaseOverview + "from airport_large where " + whereRect + " " + whereLimit);

  // Runways > 4000 feet for simplyfied runway overview
  runwayOverviewQuery = new SqlQuery(db);
  runwayOverviewQuery->prepare(
    "select length, heading, lonx, laty, primary_lonx, primary_laty, secondary_lonx, secondary_laty "
    "from runway where airport_id = :airportId and length > 4000 " + whereLimit);

  waypointsByRectQuery = new SqlQuery(dbNav);
  waypointsByRectQuery->prepare(
    "select " + waypointQueryBase + " from waypoint where " + whereRect + " " + whereLimit);

  vorsByRectQuery = new SqlQuery(dbNav);
  vorsByRectQuery->prepare("select " + vorQueryBase + " from vor where " + whereRect + " " + whereLimit);

  ndbsByRectQuery = new SqlQuery(dbNav);
  ndbsByRectQuery->prepare("select " + ndbQueryBase + " from ndb where " + whereRect + " " + whereLimit);

  markersByRectQuery = new SqlQuery(dbNav);
  markersByRectQuery->prepare(
    "select marker_id, type, ident, heading, lonx, laty "
    "from marker "
    "where " + whereRect + " " + whereLimit);

  ilsByRectQuery = new SqlQuery(db);
  ilsByRectQuery->prepare("select " + ilsQueryBase + " from ils where " + whereRect + " " + whereLimit);

  // Get all that are crossing the anti meridian too and filter them out from the query result
  airwayByRectQuery = new SqlQuery(dbNav);
  airwayByRectQuery->prepare(
    "select " + airwayQueryBase + ", right_lonx, left_lonx, bottom_laty, top_laty from airway where " +
    "not (right_lonx < :leftx or left_lonx > :rightx or bottom_laty > :topy or top_laty < :bottomy) "
    "or right_lonx < left_lonx");

  airwayByWaypointIdQuery = new SqlQuery(dbNav);
  airwayByWaypointIdQuery->prepare(
    "select " + airwayQueryBase + " from airway where from_waypoint_id = :id or to_waypoint_id = :id");

  airwayByNameAndWaypointQuery = new SqlQuery(dbNav);
  airwayByNameAndWaypointQuery->prepare(
    "select " + airwayQueryBase +
    " from airway a join waypoint wf on a.from_waypoint_id = wf.waypoint_id "
    "join waypoint wt on a.to_waypoint_id = wt.waypoint_id "
    "where a.airway_name = :airway and ((wf.ident = :ident1 and wt.ident = :ident2) or "
    " (wt.ident = :ident1 and wf.ident = :ident2))");

  airwayByIdQuery = new SqlQuery(dbNav);
  airwayByIdQuery->prepare("select " + airwayQueryBase + " from airway where airway_id = :id");

  airspaceByIdQuery = new SqlQuery(dbNav);
  airspaceByIdQuery->prepare("select " + airspaceQueryBase + " from boundary where boundary_id = :id");

  airwayWaypointByIdentQuery = new SqlQuery(dbNav);
  airwayWaypointByIdentQuery->prepare("select " + waypointQueryBase +
                                      " from waypoint w "
                                      " join airway a on w.waypoint_id = a.from_waypoint_id "
                                      "where w.ident = :waypoint and a.airway_name = :airway"
                                      " union "
                                      "select " + waypointQueryBase +
                                      " from waypoint w "
                                      " join airway a on w.waypoint_id = a.to_waypoint_id "
                                      "where w.ident = :waypoint and a.airway_name = :airway");

  airwayByNameQuery = new SqlQuery(dbNav);
  airwayByNameQuery->prepare("select " + airwayQueryBase + " from airway where airway_name = :name");

  airwayWaypointsQuery = new SqlQuery(dbNav);
  airwayWaypointsQuery->prepare("select " + airwayQueryBase + " from airway where airway_name = :name "
                                                              " order by airway_fragment_no, sequence_no");

  // Get all that are crossing the anti meridian too and filter them out from the query result
  QString airspaceRect =
    " (not (max_lonx < :leftx or min_lonx > :rightx or "
    "min_laty > :topy or max_laty < :bottomy) or max_lonx < min_lonx) and ";

  airspaceByRectQuery = new SqlQuery(dbNav);
  airspaceByRectQuery->prepare(
    "select " + airspaceQueryBase + "from boundary "
                                    "where " + airspaceRect + " type like :type");

  airspaceByRectBelowAltQuery = new SqlQuery(dbNav);
  airspaceByRectBelowAltQuery->prepare(
    "select " + airspaceQueryBase + "from boundary "
                                    "where " + airspaceRect + " type like :type and min_altitude < :alt");

  airspaceByRectAboveAltQuery = new SqlQuery(dbNav);
  airspaceByRectAboveAltQuery->prepare(
    "select " + airspaceQueryBase + "from boundary "
                                    "where " + airspaceRect + " type like :type and max_altitude > :alt");

  airspaceByRectAtAltQuery = new SqlQuery(dbNav);
  airspaceByRectAtAltQuery->prepare(
    "select " + airspaceQueryBase + "from boundary "
                                    "where "
                                    "not (max_lonx < :leftx or min_lonx > :rightx or "
                                    "min_laty > :topy or max_laty < :bottomy) and "
                                    "type like :type and "
                                    ":alt between min_altitude and max_altitude");

  airspaceLinesByIdQuery = new SqlQuery(dbNav);
  airspaceLinesByIdQuery->prepare("select geometry from boundary where boundary_id = :id");

}

void MapQuery::deInitQueries()
{
  airportCache.clear();
  waypointCache.clear();
  vorCache.clear();
  ndbCache.clear();
  markerCache.clear();
  ilsCache.clear();
  airwayCache.clear();
  airspaceCache.clear();
  airspaceLineCache.clear();
  runwayOverwiewCache.clear();

  delete airportByRectQuery;
  airportByRectQuery = nullptr;
  delete airportMediumByRectQuery;
  airportMediumByRectQuery = nullptr;
  delete airportLargeByRectQuery;
  airportLargeByRectQuery = nullptr;

  delete runwayOverviewQuery;
  runwayOverviewQuery = nullptr;

  delete waypointsByRectQuery;
  waypointsByRectQuery = nullptr;
  delete vorsByRectQuery;
  vorsByRectQuery = nullptr;
  delete ndbsByRectQuery;
  ndbsByRectQuery = nullptr;
  delete markersByRectQuery;
  markersByRectQuery = nullptr;
  delete ilsByRectQuery;
  ilsByRectQuery = nullptr;
  delete airwayByRectQuery;
  airwayByRectQuery = nullptr;

  delete airspaceByRectQuery;
  airspaceByRectQuery = nullptr;
  delete airspaceByRectBelowAltQuery;
  airspaceByRectBelowAltQuery = nullptr;
  delete airspaceByRectAboveAltQuery;
  airspaceByRectAboveAltQuery = nullptr;
  delete airspaceByRectAtAltQuery;
  airspaceByRectAtAltQuery = nullptr;

  delete airspaceLinesByIdQuery;
  airspaceLinesByIdQuery = nullptr;
  delete airspaceByIdQuery;
  airspaceByIdQuery = nullptr;

  delete airwayByWaypointIdQuery;
  airwayByWaypointIdQuery = nullptr;
  delete airwayByNameAndWaypointQuery;
  airwayByNameAndWaypointQuery = nullptr;
  delete airwayByIdQuery;
  airwayByIdQuery = nullptr;

  delete vorByIdentQuery;
  vorByIdentQuery = nullptr;
  delete ndbByIdentQuery;
  ndbByIdentQuery = nullptr;
  delete waypointByIdentQuery;
  waypointByIdentQuery = nullptr;
  delete ilsByIdentQuery;
  ilsByIdentQuery = nullptr;

  delete vorByIdQuery;
  vorByIdQuery = nullptr;
  delete ndbByIdQuery;
  ndbByIdQuery = nullptr;

  delete vorByWaypointIdQuery;
  vorByWaypointIdQuery = nullptr;
  delete ndbByWaypointIdQuery;
  ndbByWaypointIdQuery = nullptr;

  delete vorNearestQuery;
  vorNearestQuery = nullptr;
  delete ndbNearestQuery;
  ndbNearestQuery = nullptr;

  delete waypointByIdQuery;
  waypointByIdQuery = nullptr;

  delete ilsByIdQuery;
  ilsByIdQuery = nullptr;

  delete airwayWaypointByIdentQuery;
  airwayWaypointByIdentQuery = nullptr;

  delete airwayByNameQuery;
  airwayByNameQuery = nullptr;

  delete airwayWaypointsQuery;
  airwayWaypointsQuery = nullptr;
}
