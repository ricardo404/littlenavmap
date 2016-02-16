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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "sql/sqldatabase.h"

namespace atools {
namespace gui {
class Dialog;
class ErrorHandler;
}

}

namespace Ui {
class MainWindow;
}

class NavMapWidget;

class MainWindow :
  public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = 0);
  ~MainWindow();

  void tableContextMenu(const QPoint& pos);

private:
  Ui::MainWindow *ui;
  NavMapWidget *mapWidget;

  atools::gui::Dialog *dialog;
  atools::gui::ErrorHandler *errorHandler;
  void openDatabase();
  void closeDatabase();

  atools::sql::SqlDatabase db;
  QString databaseFile;
};

#endif // MAINWINDOW_H
