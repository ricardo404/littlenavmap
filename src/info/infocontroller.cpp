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

#include "logging/loggingdefs.h"
#include "route/routecontroller.h"
#include "common/weatherreporter.h"
#include "infocontroller.h"
#include <QImageReader>
#include <QSettings>
#include <gui/mainwindow.h>
#include <gui/widgetstate.h>
#include "infoquery.h"
#include "ui_mainwindow.h"
#include <common/htmlbuilder.h>
#include <common/maphtmlinfobuilder.h>
#include <common/symbolpainter.h>
#include <mapgui/mapquery.h>
#include <settings/settings.h>

const int SYMBOL_SIZE = 20;

enum TabIndex
{
  AIRPORT = 0,
  RUNWAYS = 1,
  COM = 2,
  APPROACHES = 3,
  NAVAID = 4,
  NAVMAP_LEGEND = 5,
  MAP_LEGEND = 6
};

InfoController::InfoController(MainWindow *parent, MapQuery *mapDbQuery, InfoQuery *infoDbQuery) :
  QObject(parent), mainWindow(parent), mapQuery(mapDbQuery), infoQuery(infoDbQuery)
{
  iconBackColor = QApplication::palette().color(QPalette::Active, QPalette::Base);

  info = new MapHtmlInfoBuilder(mapQuery, infoQuery, true);
}

InfoController::~InfoController()
{
  delete info;
}

void InfoController::saveState()
{
  Ui::MainWindow *ui = mainWindow->getUi();

  atools::gui::WidgetState ws("InfoWindow/Widget");
  ws.save({ui->tabWidgetInformation, ui->textEditAirportInfo, ui->textEditRunwayInfo, ui->textEditComInfo,
           ui->textEditApproachInfo, ui->textEditNavaidInfo});

  atools::settings::Settings::instance()->setValue("InfoWindow/CurrentAirportId", curAirportId);
}

void InfoController::restoreState()
{
  Ui::MainWindow *ui = mainWindow->getUi();

  atools::gui::WidgetState ws("InfoWindow/Widget");
  ws.restore({ui->tabWidgetInformation, ui->textEditAirportInfo, ui->textEditRunwayInfo, ui->textEditComInfo,
              ui->textEditApproachInfo, ui->textEditNavaidInfo});

  curAirportId = atools::settings::Settings::instance()->value("InfoWindow/CurrentAirportId", -1).toInt();

  updateAirport();
}

void InfoController::updateAirport()
{
  if(databaseLoadStatus)
    return;

  qDebug() << "InfoController::updateAirport";

  if(curAirportId != -1)
  {
    HtmlBuilder html(true);
    maptypes::MapAirport ap;
    mapQuery->getAirportById(ap, curAirportId);

    info->airportText(ap, html,
                      &mainWindow->getRouteController()->getRouteMapObjects(),
                      mainWindow->getWeatherReporter(), iconBackColor);
    mainWindow->getUi()->textEditAirportInfo->setText(html.getHtml());
  }
}

void InfoController::showInformation(maptypes::MapSearchResult result)
{
  qDebug() << "InfoController::showInformation";

  HtmlBuilder html(true);

  Ui::MainWindow *ui = mainWindow->getUi();
  int idx = ui->tabWidgetInformation->currentIndex();

  if(!result.airports.isEmpty())
  {
    if(idx != AIRPORT && idx != RUNWAYS && idx != COM && idx != APPROACHES)
      ui->tabWidgetInformation->setCurrentIndex(AIRPORT);

    curAirportId = result.airports.first().id;
    updateAirport();
  }
  else if(!result.vors.isEmpty())
  {
    ui->tabWidgetInformation->setCurrentIndex(NAVAID);

    QIcon icon = SymbolPainter().createVorIcon(result.vors.first(), SYMBOL_SIZE);
    mainWindow->getUi()->textEditNavaidInfo->document()->addResource(
      QTextDocument::ImageResource, QUrl("data://symbol"),
      QVariant(icon.pixmap(QSize(SYMBOL_SIZE, SYMBOL_SIZE))));

    info->vorText(result.vors.first(), html, iconBackColor);
    ui->textEditNavaidInfo->setText(html.getHtml());
  }
  else if(!result.ndbs.isEmpty())
  {
    ui->tabWidgetInformation->setCurrentIndex(NAVAID);

    QIcon icon = SymbolPainter().createNdbIcon(result.ndbs.first(), SYMBOL_SIZE);
    mainWindow->getUi()->textEditNavaidInfo->document()->addResource(
      QTextDocument::ImageResource, QUrl("data://symbol"),
      QVariant(icon.pixmap(QSize(SYMBOL_SIZE, SYMBOL_SIZE))));
    info->ndbText(result.ndbs.first(), html, iconBackColor);
    ui->textEditNavaidInfo->setText(html.getHtml());
  }
  else if(!result.waypoints.isEmpty())
  {
    ui->tabWidgetInformation->setCurrentIndex(NAVAID);

    QIcon icon = SymbolPainter().createWaypointIcon(result.waypoints.first(), SYMBOL_SIZE);
    mainWindow->getUi()->textEditNavaidInfo->document()->addResource(
      QTextDocument::ImageResource, QUrl("data://symbol"),
      QVariant(icon.pixmap(QSize(SYMBOL_SIZE, SYMBOL_SIZE))));
    info->waypointText(result.waypoints.first(), html, iconBackColor);
    ui->textEditNavaidInfo->setText(html.getHtml());
  }
  else if(!result.airways.isEmpty())
  {
    ui->tabWidgetInformation->setCurrentIndex(NAVAID);

    info->airwayText(result.airways.first(), html);
    ui->textEditNavaidInfo->setText(html.getHtml());
  }
}

void InfoController::preDatabaseLoad()
{
  databaseLoadStatus = true;
}

void InfoController::postDatabaseLoad()
{
  databaseLoadStatus = false;
}