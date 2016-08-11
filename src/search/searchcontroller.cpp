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

#include "search/searchcontroller.h"

#include "gui/mainwindow.h"
#include "search/column.h"
#include "search/columnlist.h"
#include "search/airportsearch.h"
#include "search/navsearch.h"
#include "mapgui/mapwidget.h"

#include <QTabWidget>

SearchController::SearchController(MainWindow *parent, MapQuery *mQuery,
                                   QTabWidget *tabWidgetSearch)
  : mapQuery(mQuery), mainWindow(parent), tabWidget(tabWidgetSearch)
{
  connect(tabWidget, &QTabWidget::currentChanged, this, &SearchController::tabChanged);

}

SearchController::~SearchController()
{
  delete airportSearch;
  delete airportColumns;

  delete navSearch;
  delete navColumns;
}

void SearchController::getSelectedMapObjects(maptypes::MapSearchResult& result) const
{
  allSearchTabs.at(tabWidget->currentIndex())->getSelectedMapObjects(result);
}

void SearchController::updateTableSelection()
{
  // Force signal to display correct status bar indication
  allSearchTabs.at(tabWidget->currentIndex())->updateTableSelection();
}

void SearchController::optionsChanged()
{
  for(SearchBase *search : allSearchTabs)
    search->optionsChanged();
}

void SearchController::tabChanged(int index)
{
  allSearchTabs.at(index)->updateTableSelection();
}

void SearchController::saveState()
{
  for(SearchBase *s : allSearchTabs)
    s->saveState();
}

void SearchController::restoreState()
{
  for(SearchBase *s : allSearchTabs)
    s->restoreState();
}

AirportSearch *SearchController::getAirportSearch() const
{
  return airportSearch;
}

NavSearch *SearchController::getNavSearch() const
{
  return navSearch;
}

void SearchController::createAirportSearch(QTableView *tableView)
{
  airportColumns = new ColumnList("airport", "airport_id");

  airportSearch = new AirportSearch(mainWindow, tableView, airportColumns, mapQuery, 0);

  airportSearch->connectSlots();

  mainWindow->getMapWidget()->connect(mainWindow->getMapWidget(), &MapWidget::searchMarkChanged,
                                      airportSearch, &SearchBase::searchMarkChanged);

  allSearchTabs.append(airportSearch);
}

void SearchController::createNavSearch(QTableView *tableView)
{
  navColumns = new ColumnList("nav_search", "nav_search_id");

  navSearch = new NavSearch(mainWindow, tableView, navColumns, mapQuery, 1);
  navSearch->connectSlots();

  mainWindow->getMapWidget()->connect(mainWindow->getMapWidget(), &MapWidget::searchMarkChanged,
                                      navSearch, &SearchBase::searchMarkChanged);

  allSearchTabs.append(navSearch);
}

void SearchController::preDatabaseLoad()
{
  for(SearchBase *search : allSearchTabs)
    search->preDatabaseLoad();
}

void SearchController::postDatabaseLoad()
{
  for(SearchBase *search : allSearchTabs)
    search->postDatabaseLoad();
}

void SearchController::showInSearch(maptypes::MapObjectTypes type, const QString& ident,
                                    const QString& region, const QString& airportIdent)
{
  qDebug() << "SearchController::objectSelected type" << type << "ident" << ident << "region" << region;

  switch(type)
  {
    case maptypes::AIRPORT:
      airportSearch->resetSearch();
      airportSearch->filterByIdent(ident);
      break;
    case maptypes::NDB:
    case maptypes::VOR:
    case maptypes::ILS:
    case maptypes::WAYPOINT:
      navSearch->resetSearch();
      navSearch->filterByIdent(ident, region, airportIdent);
      break;
    case maptypes::MARKER:
    case maptypes::NONE:
    case maptypes::ALL_NAV:
    case maptypes::ALL:
      break;
  }
}
