#include "app_theme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QSettings>
#include <QWidget>

namespace {

constexpr const char* kOrg = "NebbieArcane";
constexpr const char* kApp = "Izanagi";
constexpr const char* kThemeKey = "ui/theme";

QPalette dark_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(45, 45, 48));
  p.setColor(QPalette::WindowText, QColor(220, 220, 220));
  p.setColor(QPalette::Base, QColor(30, 30, 30));
  // Alternanza righe appena accennata: NON deve somigliare a barre e titoli.
  p.setColor(QPalette::AlternateBase, QColor(36, 36, 38));
  p.setColor(QPalette::ToolTipBase, QColor(45, 45, 48));
  p.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
  p.setColor(QPalette::Text, QColor(220, 220, 220));
  p.setColor(QPalette::Button, QColor(55, 55, 58));
  p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
  p.setColor(QPalette::BrightText, QColor(255, 80, 80));
  p.setColor(QPalette::Highlight, QColor(42, 130, 218));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::PlaceholderText, QColor(140, 140, 140));
  p.setColor(QPalette::Link, QColor(90, 160, 255));
  return p;
}

QPalette light_palette() {
  QPalette p;
  p.setColor(QPalette::Window, QColor(245, 245, 245));
  p.setColor(QPalette::WindowText, QColor(20, 20, 20));
  p.setColor(QPalette::Base, QColor(255, 255, 255));
  p.setColor(QPalette::AlternateBase, QColor(247, 247, 247));
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 240));
  p.setColor(QPalette::ToolTipText, QColor(20, 20, 20));
  p.setColor(QPalette::Text, QColor(20, 20, 20));
  p.setColor(QPalette::Button, QColor(232, 232, 232));
  p.setColor(QPalette::ButtonText, QColor(20, 20, 20));
  p.setColor(QPalette::BrightText, QColor(200, 0, 0));
  p.setColor(QPalette::Highlight, QColor(0, 120, 215));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
  p.setColor(QPalette::Link, QColor(0, 102, 204));
  return p;
}

QPalette classic_palette() {
  // Win95 / Delphi Eleutheria: #d4d0c8
  QPalette p;
  p.setColor(QPalette::Window, QColor(0xd4, 0xd0, 0xc8));
  p.setColor(QPalette::WindowText, QColor(0, 0, 0));
  p.setColor(QPalette::Base, QColor(255, 255, 255));
  p.setColor(QPalette::AlternateBase, QColor(0xf6, 0xf4, 0xf0));
  p.setColor(QPalette::ToolTipBase, QColor(255, 255, 225));
  p.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
  p.setColor(QPalette::Text, QColor(0, 0, 0));
  p.setColor(QPalette::Button, QColor(0xd4, 0xd0, 0xc8));
  p.setColor(QPalette::ButtonText, QColor(0, 0, 0));
  p.setColor(QPalette::BrightText, QColor(255, 255, 255));
  p.setColor(QPalette::Highlight, QColor(0x0a, 0x24, 0x6a));
  p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
  p.setColor(QPalette::PlaceholderText, QColor(100, 100, 100));
  p.setColor(QPalette::Link, QColor(0x0a, 0x24, 0x6a));
  p.setColor(QPalette::Light, QColor(255, 255, 255));
  p.setColor(QPalette::Midlight, QColor(0xe0, 0xdc, 0xd4));
  p.setColor(QPalette::Dark, QColor(0x80, 0x80, 0x80));
  p.setColor(QPalette::Mid, QColor(0xa0, 0xa0, 0xa0));
  p.setColor(QPalette::Shadow, QColor(0x40, 0x40, 0x40));
  return p;
}

const char* dark_qss() {
  return R"(
    /* Tre livelli: COMANDI (scuri) / TITOLI (intermedi) / CONTENUTO (neutro) */
    QToolTip { color: #ddd; background: #2d2d30; border: 1px solid #555; }
    QMainWindow { background: #121213; }
    QMainWindow::separator { background: #121213; width: 6px; height: 6px; }
    QMainWindow::separator:hover { background: #007acc; }

    /* Comandi */
    QMenuBar { background: #18181a; border-bottom: 1px solid #3a3a3e; padding: 2px; }
    QMenuBar::item { padding: 4px 10px; background: transparent; }
    QMenuBar::item:selected { background: #3e3e42; border-radius: 3px; }
    QMenu { background: #252527; border: 1px solid #555; }
    QMenu::item { padding: 5px 24px 5px 20px; }
    QMenu::item:selected { background: #094771; }
    QMenu::separator { height: 1px; background: #3e3e42; margin: 4px 8px; }
    QToolBar { background: #18181a; border: none; border-bottom: 1px solid #3a3a3e; padding: 3px; spacing: 3px; }
    QToolBar QToolButton { color: #d8d8d8; padding: 3px 9px; border: 1px solid #3a3a3e; border-radius: 3px; background: #242427; }
    QToolBar QToolButton:hover { background: #2f2f34; border-color: #007acc; }
    QToolBar QToolButton:pressed { background: #094771; }
    QToolBar::separator { width: 1px; background: #3a3a3e; margin: 3px 6px; }

    /* Titoli dei pannelli e intestazioni */
    QDockWidget { color: #f0f0f0; font-weight: bold; }
    QDockWidget::title { background: #333338; padding: 6px 8px; border-left: 3px solid #007acc; }
    QHeaderView::section { background: #2a2a2e; color: #e0e0e0; font-weight: bold; border: none; border-right: 1px solid #3a3a3e; border-bottom: 1px solid #4a4a50; padding: 5px; }
    QTabWidget::pane { border: 1px solid #3a3a3e; top: -1px; }
    QTabBar::tab { background: #232326; color: #b8b8b8; padding: 6px 14px; border: 1px solid #3a3a3e; border-bottom: none; margin-right: 2px; }
    QTabBar::tab:selected { background: #1e1e1e; color: #ffffff; border-top: 2px solid #007acc; }
    QTabBar::tab:hover:!selected { background: #2c2c30; }

    /* Contenuto */
    QDockWidget > QWidget { font-weight: normal; background: #1e1e1e; }
    QTableView, QListView, QTreeView { background: #1e1e1e; border: none; gridline-color: #2a2a2d; selection-background-color: #094771; }

    QStatusBar { background: #007acc; color: #fff; }
  )";
}

const char* light_qss() {
  return R"(
    QToolTip { color: #222; background: #ffffe1; border: 1px solid #888; }
    QMainWindow::separator { background: #b4b4b8; width: 6px; height: 6px; }
    QMainWindow::separator:hover { background: #0078d7; }

    QMenuBar { background: #e4e4e6; border-bottom: 1px solid #b8b8bc; padding: 2px; }
    QMenuBar::item { padding: 4px 10px; background: transparent; }
    QMenuBar::item:selected { background: #cce4f7; border-radius: 3px; }
    QMenu { background: #ffffff; border: 1px solid #aaa; }
    QMenu::item { padding: 5px 24px 5px 20px; }
    QMenu::item:selected { background: #0078d7; color: #fff; }
    QMenu::separator { height: 1px; background: #ddd; margin: 4px 8px; }
    QToolBar { background: #ececee; border: none; border-bottom: 1px solid #c4c4c8; padding: 3px; spacing: 3px; }
    QToolBar QToolButton { color: #222; padding: 3px 9px; border: 1px solid #c4c4c8; border-radius: 3px; background: #fafafa; }
    QToolBar QToolButton:hover { background: #e5f1fb; border-color: #0078d7; }
    QToolBar QToolButton:pressed { background: #cce4f7; }
    QToolBar::separator { width: 1px; background: #c4c4c8; margin: 3px 6px; }

    QDockWidget { color: #111; font-weight: bold; }
    QDockWidget::title { background: #d6d6da; padding: 6px 8px; border-left: 3px solid #0078d7; }
    QHeaderView::section { background: #ececee; color: #222; font-weight: bold; border: none; border-right: 1px solid #d0d0d4; border-bottom: 1px solid #b8b8bc; padding: 5px; }
    QTabWidget::pane { border: 1px solid #c4c4c8; top: -1px; }
    QTabBar::tab { background: #e6e6e8; color: #444; padding: 6px 14px; border: 1px solid #c4c4c8; border-bottom: none; margin-right: 2px; }
    QTabBar::tab:selected { background: #ffffff; color: #000; border-top: 2px solid #0078d7; }

    QDockWidget > QWidget { font-weight: normal; background: #ffffff; }
    QTableView, QListView, QTreeView { background: #ffffff; border: none; gridline-color: #eeeeee; }

    QStatusBar { background: #0078d7; color: #fff; }
  )";
}

const char* classic_qss() {
  return R"(
    QMainWindow, QWidget { background: #d4d0c8; color: #000000; }
    QMainWindow::separator { background: #a8a49c; width: 6px; height: 6px; }
    QToolTip { color: #000; background: #ffffe1; border: 1px solid #000; }
    QMenuBar { background: #d4d0c8; border-bottom: 1px solid #808080; }
    QMenuBar::item:selected { background: #0a246a; color: white; }
    QMenu { background: #d4d0c8; border: 1px solid #808080; }
    QMenu::item:selected { background: #0a246a; color: white; }
    QToolBar { background: #c8c4bc; border: none; border-bottom: 1px solid #808080; padding: 2px; spacing: 3px; }
    QToolBar QToolButton { padding: 2px 8px; border: 1px solid #808080; background: #d4d0c8; }
    QToolBar QToolButton:hover { border-color: #0a246a; }
    QToolBar QToolButton:pressed { background: #c0bcb4; }
    QListWidget, QPlainTextEdit, QLineEdit, QComboBox, QTableWidget, QTreeWidget {
      background: #ffffff; border: 1px solid #808080; color: #000;
    }
    QTabWidget::pane { border: 1px solid #808080; background: #d4d0c8; }
    QTabBar::tab { background: #d4d0c8; padding: 6px 12px; border: 1px solid #808080; color: #000; }
    QTabBar::tab:selected { background: #ffffff; }
    QHeaderView::section { background: #d4d0c8; color: #000; font-weight: bold; border: 1px solid #808080; padding: 4px; }
    QStatusBar { background: #d4d0c8; color: #000; border-top: 1px solid #808080; }
    QDockWidget { color: #ffffff; font-weight: bold; }
    QDockWidget::title { background: #0a246a; padding: 5px 8px; }
    QDockWidget > QWidget { font-weight: normal; color: #000; }
    QPushButton { background: #d4d0c8; border: 1px solid #808080; padding: 3px 10px; }
    QPushButton:pressed { background: #c0bcb4; }
    QCheckBox, QLabel, QRadioButton { color: #000; background: transparent; }
  )";
}

}  // namespace

AppTheme theme_from_key(const QString& key) {
  if(key == QLatin1String("light")) {
    return AppTheme::Light;
  }
  if(key == QLatin1String("classic") || key == QLatin1String("gray") ||
     key == QLatin1String("eleutheria")) {
    return AppTheme::ClassicGray;
  }
  return AppTheme::Dark;
}

QString theme_to_key(AppTheme theme) {
  switch(theme) {
    case AppTheme::Light:
      return QStringLiteral("light");
    case AppTheme::ClassicGray:
      return QStringLiteral("classic");
    case AppTheme::Dark:
    default:
      return QStringLiteral("dark");
  }
}

QString theme_label(AppTheme theme) {
  switch(theme) {
    case AppTheme::Light:
      return QStringLiteral("Chiaro");
    case AppTheme::ClassicGray:
      return QStringLiteral("Grigio anni '90");
    case AppTheme::Dark:
    default:
      return QStringLiteral("Scuro");
  }
}

void apply_app_theme(QWidget* root, AppTheme theme) {
  qApp->setStyle(QStringLiteral("Fusion"));
  QPalette palette;
  const char* qss = dark_qss();
  switch(theme) {
    case AppTheme::Light:
      palette = light_palette();
      qss = light_qss();
      break;
    case AppTheme::ClassicGray:
      palette = classic_palette();
      qss = classic_qss();
      break;
    case AppTheme::Dark:
    default:
      palette = dark_palette();
      qss = dark_qss();
      break;
  }
  qApp->setPalette(palette);
  if(root != nullptr) {
    root->setStyleSheet(QString::fromUtf8(qss));
  } else {
    qApp->setStyleSheet(QString::fromUtf8(qss));
  }
}

AppTheme load_saved_theme() {
  QSettings settings(QString::fromUtf8(kOrg), QString::fromUtf8(kApp));
  return theme_from_key(settings.value(QString::fromUtf8(kThemeKey), QStringLiteral("dark")).toString());
}

void save_theme(AppTheme theme) {
  QSettings settings(QString::fromUtf8(kOrg), QString::fromUtf8(kApp));
  settings.setValue(QString::fromUtf8(kThemeKey), theme_to_key(theme));
}
