/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
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

#include "maptypes.h"

#include <QHash>
#include <QObject>

namespace maptypes {

const QHash<QString, QString> surfaceMap(
  {
    {"CONCRETE", QObject::tr("Concrete")},
    {"GRASS", QObject::tr("Grass")},
    {"WATER", QObject::tr("Water")},
    {"ASPHALT", QObject::tr("Asphalt")},
    {"CEMENT", QObject::tr("Cement")},
    {"CLAY", QObject::tr("Clay")},
    {"SNOW", QObject::tr("Snow")},
    {"ICE", QObject::tr("Ice")},
    {"DIRT", QObject::tr("Dirt")},
    {"CORAL", QObject::tr("Coral")},
    {"GRAVEL", QObject::tr("Gravel")},
    {"OIL_TREATED", QObject::tr("Oil treated")},
    {"STEEL_MATS", QObject::tr("Steel Mats")},
    {"BITUMINOUS", QObject::tr("Bituminous")},
    {"BRICK", QObject::tr("Brick")},
    {"MACADAM", QObject::tr("Macadam")},
    {"PLANKS", QObject::tr("Planks")},
    {"SAND", QObject::tr("Sand")},
    {"SHALE", QObject::tr("Shale")},
    {"TARMAC", QObject::tr("Tarmac")},
    {"UNKNOWN", QObject::tr("Unknown")}
  });

const QHash<QString, QString> parkingMapGate(
  {
    {"UNKNOWN", QObject::tr("Unknown")},
    {"RAMP_GA", QObject::tr("Ramp GA")},
    {"RAMP_GA_SMALL", QObject::tr("Ramp GA Small")},
    {"RAMP_GA_MEDIUM", QObject::tr("Ramp GA Medium")},
    {"RAMP_GA_LARGE", QObject::tr("Ramp GA Large")},
    {"RAMP_CARGO", QObject::tr("Ramp Cargo")},
    {"RAMP_MIL_CARGO", QObject::tr("Ramp Mil Cargo")},
    {"RAMP_MIL_COMBAT", QObject::tr("Ramp Mil Combat")},
    {"GATE_SMALL", QObject::tr("Small")},
    {"GATE_MEDIUM", QObject::tr("Medium")},
    {"GATE_HEAVY", QObject::tr("Heavy")},
    {"DOCK_GA", QObject::tr("Dock GA")},
    {"FUEL", QObject::tr("Fuel")},
    {"VEHICLES", QObject::tr("Vehicles")}
  });

const QHash<QString, QString> parkingMapRamp(
  {
    {"UNKNOWN", QObject::tr("Unknown")},
    {"RAMP_GA", QObject::tr("Ramp GA")},
    {"RAMP_GA_SMALL", QObject::tr("Small")},
    {"RAMP_GA_MEDIUM", QObject::tr("Medium")},
    {"RAMP_GA_LARGE", QObject::tr("Large")},
    {"RAMP_CARGO", QObject::tr("Ramp Cargo")},
    {"RAMP_MIL_CARGO", QObject::tr("Ramp Mil Cargo")},
    {"RAMP_MIL_COMBAT", QObject::tr("Ramp Mil Combat")},
    {"GATE_SMALL", QObject::tr("Gate Small")},
    {"GATE_MEDIUM", QObject::tr("Gate Medium")},
    {"GATE_HEAVY", QObject::tr("Gate Heavy")},
    {"DOCK_GA", QObject::tr("Dock GA")},
    {"FUEL", QObject::tr("Fuel")},
    {"VEHICLES", QObject::tr("Vehicles")}
  });

const QHash<QString, QString> typeNames(
  {
    {"HIGH", "High"},
    {"LOW", "Low"},
    {"TERMINAL", "Terminal"},
    {"HH", "HH"},
    {"H", "H"},
    {"MH", "MH"},
    {"COMPASS_POINT", "Compass Point"},
    {"NAMED", "Named"},
    {"UNNAMED", "Unnamed"},
    {"VOR", "VOR"},
    {"NDB", "NDB"}
  });

const QHash<QString, QString> navTypeNames(
  {
    {"VORDME", "VORDME"},
    {"VOR", "VOR"},
    {"DME", "DME"},
    {"NDB", "NDB"},
    {"WAYPOINT", "Waypoint"}
  });

QString navTypeName(const QString& type)
{
  return typeNames.value(type);
}

QString navName(const QString& type)
{
  return navTypeNames.value(type);
}

QString surfaceName(const QString& surface)
{
  return surfaceMap.value(surface);
}

QString parkingGateName(const QString& gate)
{
  return parkingMapGate.value(gate);
}

QString parkingRampName(const QString& ramp)
{
  return parkingMapRamp.value(ramp);
}

MapSearchResult::~MapSearchResult()
{
  if(needsDelete)
  {
    qDeleteAll(airports);
    qDeleteAll(towers);
    qDeleteAll(parkings);
    qDeleteAll(helipads);
    qDeleteAll(waypoints);
    qDeleteAll(vors);
    qDeleteAll(ndbs);
    qDeleteAll(markers);
    qDeleteAll(ils);

    airports.clear();
    towers.clear();
    parkings.clear();
    helipads.clear();
    waypoints.clear();
    vors.clear();
    ndbs.clear();
    markers.clear();
    ils.clear();
  }
}

} // namespace types