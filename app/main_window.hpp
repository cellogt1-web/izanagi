#pragma once

#include "app_theme.hpp"
#include "eleutheria/catalog.hpp"
#include "eleutheria/types.hpp"
#include "eleutheria/workspace.hpp"
#include "eleutheria/workspace_index.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <optional>
#include <vector>

class QAction;
class QActionGroup;
class QMenu;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QDockWidget;
class AreaPanel;

// Finestra principale modulare: in alto solo i menu; tutto il resto sono pannelli
// (QDockWidget) spostabili, staccabili su altri schermi e nascondibili.
//  - pannello Workspace (elenco aree)
//  - un pannello per ogni area aperta (piu' aree insieme)
//  - segnaposto "nessuna area aperta" quando non ce ne sono
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  void openPath(const QString& path);

 protected:
  void closeEvent(QCloseEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void newArea();
  void openAny();
  void manageWorkspaces();
  void addAreaToWorkspace();
  void saveActiveArea();
  void saveActiveAreaAs();
  void saveAllAreas();
  void closeActiveArea();
  void validateActiveArea();
  void about();

  void onWorkspaceAreaActivated(QTableWidgetItem* item);
  void setTheme(AppTheme theme);
  void setWorkspaceColumnVisible(int logical_column, bool visible);
  void editLineLengthLimit();

 private:
  void buildUi();
  QDockWidget* buildWorkspaceDock();
  void applyCurrentTheme();
  void applyMaxLineLength();
  void loadMaxLineLength();
  void saveMaxLineLength();
  void refreshWorkspaceList();
  void setupWorkspaceColumnsMenu(QMenu* parent_menu);
  void applyWorkspaceColumnVisibility();
  void saveWorkspaceTableState();
  void restoreWorkspaceTableState();
  void updateTitle();
  void updateStatusCounts();
  void loadWorkspace(const QString& path, bool interactive = true);
  void showWorkspaceManager(const QString& warning);
  QString areasWithNameProblems() const;
  void saveLastWorkspacePath(const QString& path);
  void restoreLastWorkspace();
  void rebuildWorkspaceIndex();
  void openSelectedWorkspaceArea();

  // Pannelli area
  AreaPanel* addAreaPanel(eleutheria::Area area, const QString& base_path);
  AreaPanel* findOpenArea(const QString& base_path) const;
  bool closeAreaPanel(AreaPanel* panel);  // false = annullato
  void setActivePanel(AreaPanel* panel);
  AreaPanel* activePanel() const;
  void updateAreaPlaceholder();
  void setAreaWindowsVisible(bool visible);

  eleutheria::Catalog catalog_;

  std::optional<eleutheria::Workspace> workspace_;
  std::vector<eleutheria::WorkspaceArea> workspace_areas_;
  eleutheria::WorkspaceEntityIndex workspace_index_;
  bool workspace_index_ready_ = false;

  QTableWidget* workspace_table_ = nullptr;
  QDockWidget* workspace_dock_ = nullptr;
  QDockWidget* area_placeholder_dock_ = nullptr;
  QAction* workspace_toggle_action_ = nullptr;
  QAction* area_toggle_action_ = nullptr;
  std::vector<QAction*> workspace_column_actions_;
  QLabel* status_counts_ = nullptr;
  QActionGroup* theme_group_ = nullptr;

  std::vector<QDockWidget*> area_docks_;
  QPointer<AreaPanel> active_panel_;
  int area_dock_counter_ = 0;

  AppTheme current_theme_ = AppTheme::Dark;
  int max_line_length_ = 0;
};
