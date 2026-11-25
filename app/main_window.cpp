#include "main_window.hpp"

#include "app_theme.hpp"
#include "area_panel.hpp"
#include "project_panel.hpp"
#include "project_store.hpp"
#include "workspace_tree.hpp"
#include "editors.hpp"
#include "file_dialogs.hpp"
#include "workspace_manager.hpp"

#include "eleutheria/area_io.hpp"
#include "eleutheria/workspace.hpp"
#include "eleutheria/workspace_index.hpp"
#include "eleutheria/world.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QDockWidget>
#include <QEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractItemView>
#include <QActionGroup>
#include <QHeaderView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidgetItem>
#include <QHash>
#include <QSet>
#include <QPoint>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVariant>
#include <QVBoxLayout>

#include <filesystem>
#include <stdexcept>

namespace {

constexpr int kWorkspaceColumnCount = 8;
constexpr const char* kWorkspaceHeaderStateKey = "ui/workspaceHeaderState3";
constexpr const char* kWorkspaceColumnVisibleKey = "ui/workspaceColumnVisible2";
constexpr const char* kLastWorkspacePathKey = "ui/lastWorkspacePath";
constexpr const char* kMaxLineLengthKey = "ui/maxLineLength";
constexpr const char* kWindowGeometryKey = "ui/mainWindowGeometry";
constexpr const char* kWindowStateKey = "ui/mainWindowState";
constexpr int kWindowStateVersion = 2;

const QStringList& workspace_column_labels() {
  static const QStringList labels = {
      QStringLiteral("Cartella"), QStringLiteral("Nome"),   QStringLiteral("Autore"),
      QStringLiteral("Zona"),     QStringLiteral("Top vnum"), QStringLiteral("Stanze"),
      QStringLiteral("Mob"),      QStringLiteral("Oggetti"),
  };
  return labels;
}

QString strip_zone_author(QString name, QString* author_out) {
  const int open = name.lastIndexOf(QLatin1Char('['));
  const int close = name.lastIndexOf(QLatin1Char(']'));
  if(open >= 0 && close > open) {
    if(author_out != nullptr) {
      *author_out = name.mid(open + 1, close - open - 1).trimmed();
    }
    name = name.left(open).trimmed();
  } else if(author_out != nullptr) {
    author_out->clear();
  }
  return name;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), catalog_(eleutheria::Catalog::nebbie_defaults()) {
  setWindowTitle("Izanagi (Nebbie Arcane)");
  resize(1180, 760);
  current_theme_ = load_saved_theme();
  applyCurrentTheme();
  loadMaxLineLength();
  projects_ = new ProjectStore(this);
  buildUi();
  {
    QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
    restoreGeometry(settings.value(QString::fromUtf8(kWindowGeometryKey)).toByteArray());
    restoreState(settings.value(QString::fromUtf8(kWindowStateKey)).toByteArray(),
                 kWindowStateVersion);
  }
  updateAreaPlaceholder();
  updateStatusCounts();
  qApp->installEventFilter(this);
  // Dopo show()/event loop: su macOS Intel setOverrideCursor nel costruttore
  // crasha in CGImageCreate (Qt WaitCursor). Anche l'indice e' lavoro pesante.
  QTimer::singleShot(0, this, &MainWindow::restoreLastWorkspace);
}

void MainWindow::applyCurrentTheme() {
  apply_app_theme(this, current_theme_);
}

void MainWindow::setTheme(AppTheme theme) {
  current_theme_ = theme;
  save_theme(theme);
  applyCurrentTheme();
  if(theme_group_ != nullptr) {
    for(auto* action : theme_group_->actions()) {
      if(static_cast<AppTheme>(action->data().toInt()) == theme) {
        action->setChecked(true);
        break;
      }
    }
  }
}

void MainWindow::loadMaxLineLength() {
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  int value = settings.value(QString::fromUtf8(kMaxLineLengthKey), 0).toInt();
  if(value < 0) {
    value = 0;
  }
  if(value > 512) {
    value = 512;
  }
  max_line_length_ = value;
}

void MainWindow::saveMaxLineLength() {
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  settings.setValue(QString::fromUtf8(kMaxLineLengthKey), max_line_length_);
}

void MainWindow::applyMaxLineLength() {
  for(auto* dock : area_docks_) {
    if(auto* panel = qobject_cast<AreaPanel*>(dock->widget())) {
      panel->setMaxLineLength(max_line_length_);
    }
  }
}

void MainWindow::editLineLengthLimit() {
  bool ok = false;
  const int value = QInputDialog::getInt(
      this, QStringLiteral("Limite caratteri per riga"),
      QStringLiteral(
          "Numero massimo di caratteri per riga nei testi MUD\n"
          "(descrizioni stanze, mob, oggetti).\n"
          "Imposta 0 per disattivare la barra."),
      max_line_length_, 0, 512, 1, &ok);
  if(!ok) {
    return;
  }
  max_line_length_ = value;
  saveMaxLineLength();
  applyMaxLineLength();
  if(max_line_length_ > 0) {
    statusBar()->showMessage(
        QStringLiteral("Limite righe impostato a %1 caratteri.").arg(max_line_length_), 5000);
  } else {
    statusBar()->showMessage(QStringLiteral("Barra limite caratteri disattivata."), 5000);
  }
}

void MainWindow::buildUi() {
  setDockNestingEnabled(true);
  setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                 QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  // Il centro resta vuoto: tutto lo spazio va ai pannelli (struttura modulare).
  auto* center = new QWidget(this);
  center->setMaximumSize(0, 0);
  setCentralWidget(center);

  // ---------- Menu File ----------
  auto* file_menu = menuBar()->addMenu(QStringLiteral("&File"));
  file_menu->addAction(QStringLiteral("Gestione &workspace..."),
                       QKeySequence(QStringLiteral("Ctrl+Shift+W")), this,
                       &MainWindow::manageWorkspaces);
  file_menu->addAction(QStringLiteral("&Apri..."), QKeySequence::Open, this, &MainWindow::openAny);
  file_menu->addAction(QStringLiteral("&Nuova area"), QKeySequence::New, this,
                       &MainWindow::newArea);
  file_menu->addAction(QStringLiteral("Aggiungi area al workspace..."), this,
                       &MainWindow::addAreaToWorkspace);
  file_menu->addSeparator();
  file_menu->addAction(QStringLiteral("&Salva area"), QKeySequence::Save, this,
                       &MainWindow::saveActiveArea);
  file_menu->addAction(QStringLiteral("Salva area &come..."), this, &MainWindow::saveActiveAreaAs);
  file_menu->addAction(QStringLiteral("Salva &tutto"), QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                       this, &MainWindow::saveAllAreas);
  file_menu->addAction(QStringLiteral("C&hiudi area"), QKeySequence(QStringLiteral("Ctrl+W")), this,
                       &MainWindow::closeActiveArea);
  file_menu->addSeparator();
  file_menu->addAction(QStringLiteral("&Esci"), QKeySequence::Quit, this, &QWidget::close);

  // ---------- Pannelli ----------
  workspace_dock_ = buildWorkspaceDock();
  addDockWidget(Qt::LeftDockWidgetArea, workspace_dock_);

  area_placeholder_dock_ = new QDockWidget(QStringLiteral("Edit area"), this);
  area_placeholder_dock_->setObjectName(QStringLiteral("areaPlaceholderDock"));
  area_placeholder_dock_->setFeatures(QDockWidget::DockWidgetMovable |
                                      QDockWidget::DockWidgetFloatable);
  auto* placeholder = new QLabel(
      QStringLiteral("<div style='font-size:15px'><b>Nessuna area aperta</b></div><br>"
                     "Doppio clic su un'area nel pannello <b>Workspace</b>,<br>"
                     "oppure <b>File &rarr; Apri...</b> / <b>Nuova area</b>.<br><br>"
                     "Puoi aprire piu' aree insieme: ognuna ha il suo pannello,<br>"
                     "che puoi spostare, affiancare o staccare su un altro schermo."),
      area_placeholder_dock_);
  placeholder->setAlignment(Qt::AlignCenter);
  placeholder->setTextFormat(Qt::RichText);
  area_placeholder_dock_->setWidget(placeholder);
  addDockWidget(Qt::RightDockWidgetArea, area_placeholder_dock_);
  resizeDocks({workspace_dock_, area_placeholder_dock_}, {620, 760}, Qt::Horizontal);

  // ---------- Menu Visualizza ----------
  auto* view_menu = menuBar()->addMenu(QStringLiteral("&Visualizza"));
  workspace_toggle_action_ = workspace_dock_->toggleViewAction();
  workspace_toggle_action_->setText(QStringLiteral("Finestra workspace"));
  workspace_toggle_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+A")));
  view_menu->addAction(workspace_toggle_action_);
  area_toggle_action_ = view_menu->addAction(QStringLiteral("Finestre edit area"));
  area_toggle_action_->setCheckable(true);
  area_toggle_action_->setChecked(true);
  area_toggle_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
  connect(area_toggle_action_, &QAction::toggled, this, &MainWindow::setAreaWindowsVisible);
  view_menu->addSeparator();
  auto* theme_menu = view_menu->addMenu(QStringLiteral("Tema"));
  theme_group_ = new QActionGroup(this);
  theme_group_->setExclusive(true);
  const AppTheme themes[] = {AppTheme::Dark, AppTheme::Light, AppTheme::ClassicGray};
  for(AppTheme theme : themes) {
    auto* action = theme_menu->addAction(theme_label(theme));
    action->setCheckable(true);
    action->setData(static_cast<int>(theme));
    theme_group_->addAction(action);
    if(theme == current_theme_) {
      action->setChecked(true);
    }
    connect(action, &QAction::triggered, this, [this, theme]() { setTheme(theme); });
  }
  setupWorkspaceColumnsMenu(view_menu);
  applyWorkspaceColumnVisibility();
  view_menu->addSeparator();
  view_menu->addAction(QStringLiteral("Ripristina disposizione pannelli"), this, [this]() {
    workspace_dock_->setFloating(false);
    addDockWidget(Qt::LeftDockWidgetArea, workspace_dock_);
    workspace_dock_->show();
    area_placeholder_dock_->setFloating(false);
    addDockWidget(Qt::RightDockWidgetArea, area_placeholder_dock_);
    std::vector<QDockWidget*> all = project_docks_;
    all.insert(all.end(), area_docks_.begin(), area_docks_.end());
    for(auto* dock : all) {
      dock->setFloating(false);
      addDockWidget(Qt::RightDockWidgetArea, dock);
      tabifyDockWidget(area_placeholder_dock_, dock);
      dock->show();
    }
    area_toggle_action_->setChecked(true);
    updateAreaPlaceholder();
    resizeDocks({workspace_dock_, area_placeholder_dock_}, {420, width() - 420},
                Qt::Horizontal);
  });

  // ---------- Menu Strumenti ----------
  auto* tools_menu = menuBar()->addMenu(QStringLiteral("&Strumenti"));
  tools_menu->addAction(QStringLiteral("&Valida area"), this, &MainWindow::validateActiveArea);
  tools_menu->addSeparator();
  tools_menu->addAction(QStringLiteral("Limite caratteri per &riga..."), this,
                        &MainWindow::editLineLengthLimit);

  auto* help_menu = menuBar()->addMenu(QStringLiteral("&?"));
  help_menu->addAction(QStringLiteral("Informazioni su Izanagi..."), this, &MainWindow::about);

  status_counts_ = new QLabel(this);
  statusBar()->addWidget(status_counts_, 1);
}

QDockWidget* MainWindow::buildWorkspaceDock() {
  auto* dock = new QDockWidget(QStringLiteral("Workspace"), this);
  dock->setObjectName(QStringLiteral("workspaceDock"));
  dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);

  auto* container = new QWidget(dock);
  auto* layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // Barra comandi del pannello Workspace
  auto* tb = new QToolBar(container);
  tb->setObjectName(QStringLiteral("workspaceToolbar"));
  tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
  tb->addAction(QStringLiteral("Gestione..."), this, &MainWindow::manageWorkspaces)
      ->setToolTip(QStringLiteral("Elenco dei workspace: apri, crea, aggiungi"));
  tb->addAction(QStringLiteral("Apri area"), this, &MainWindow::openSelectedWorkspaceArea)
      ->setToolTip(QStringLiteral("Apre l'area selezionata (oppure doppio clic)"));
  tb->addAction(QStringLiteral("Aggiungi area..."), this, &MainWindow::addAreaToWorkspace)
      ->setToolTip(QStringLiteral("Copia nel workspace un'area presa da un'altra cartella"));
  tb->addAction(QStringLiteral("Nuovo progetto..."), this, &MainWindow::newProject)
      ->setToolTip(QStringLiteral("Crea un progetto per raggruppare aree (solo organizzazione: "
                                  "le aree non vengono modificate)"));
  tb->addAction(QStringLiteral("Aggiorna"), this, [this]() {
      if(workspace_) {
        try {
          loadWorkspace(QString::fromStdString(workspace_->root.string()), false);
        } catch(const std::exception& e) {
          QMessageBox::critical(this, "Errore workspace", QString::fromUtf8(e.what()));
        }
      }
    })->setToolTip(QStringLiteral("Rilegge le aree dal disco"));
  layout->addWidget(tb);

  workspace_table_ = new ws_tree::Tree(container);
  workspace_table_->setColumnCount(kWorkspaceColumnCount);
  workspace_table_->setHeaderLabels(workspace_column_labels());
  workspace_table_->setAlternatingRowColors(true);
  workspace_table_->setSortingEnabled(true);
  workspace_table_->setContextMenuPolicy(Qt::CustomContextMenu);
  auto* header = workspace_table_->header();
  header->setStretchLastSection(true);
  header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  header->setSectionsMovable(true);
  header->setSectionsClickable(true);
  header->setSortIndicatorShown(true);
  header->setSectionResizeMode(QHeaderView::Interactive);
  header->setMinimumSectionSize(48);
  header->setSortIndicator(0, Qt::AscendingOrder);
  for(int col = 0; col < kWorkspaceColumnCount; ++col) {
    header->resizeSection(col, col == 0 ? 150 : col == 1 ? 180 : (col >= 3 ? 64 : 110));
  }
  workspace_table_->setMinimumWidth(320);
  layout->addWidget(workspace_table_, 1);
  dock->setWidget(container);

  restoreWorkspaceTableState();
  connect(header, &QHeaderView::sectionMoved, this, [this](int, int, int) {
    saveWorkspaceTableState();
  });
  connect(workspace_table_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem* item, int) { onWorkspaceAreaActivated(item); });
  connect(workspace_table_, &QWidget::customContextMenuRequested, this,
          &MainWindow::showWorkspaceContextMenu);
  connect(workspace_table_, &ws_tree::Tree::areasDropped, this,
          &MainWindow::assignAreasToProject);
  return dock;
}

void MainWindow::rebuildWorkspaceIndex() {
  workspace_index_.clear();
  workspace_index_ready_ = false;
  if(workspace_ && !workspace_areas_.empty()) {
    statusBar()->showMessage(
        QStringLiteral("Indicizzazione workspace (%1 aree)...").arg(workspace_areas_.size()));
    // Non usare QApplication::setOverrideCursor: su macOS Intel (Qt 6.11 + cocoa)
    // puo' crashare in QImage::toCGImage / CGImageCreate, soprattutto all'avvio.
    try {
      workspace_index_.build_from_workspace(*workspace_, workspace_areas_);
      workspace_index_ready_ = true;
    } catch(...) {
      workspace_index_.clear();
      workspace_index_ready_ = false;
    }
    statusBar()->showMessage(
        QStringLiteral("Indice workspace: %1 mob, %2 obj, %3 stanze")
            .arg(workspace_index_.mobiles.size())
            .arg(workspace_index_.objects.size())
            .arg(workspace_index_.rooms.size()),
        8000);
  }
  for(auto* dock : area_docks_) {
    if(auto* panel = qobject_cast<AreaPanel*>(dock->widget())) {
      panel->setWorkspaceIndex(&workspace_index_, workspace_index_ready_);
    }
  }
}

void MainWindow::updateTitle() {
  QString title = QStringLiteral("Izanagi (Nebbie Arcane)");
  if(workspace_) {
    title += QStringLiteral(" [") + QString::fromStdString(workspace_->root.filename().string()) +
             QStringLiteral("]");
  }
  if(auto* panel = activePanel()) {
    title += QStringLiteral(" - ") + panel->displayName();
  }
  setWindowTitle(title);
  if(workspace_dock_ != nullptr) {
    workspace_dock_->setWindowTitle(
        workspace_ ? QStringLiteral("Workspace: %1 (%2 aree)")
                         .arg(QString::fromStdString(workspace_->root.filename().string()))
                         .arg(workspace_areas_.size())
                   : QStringLiteral("Workspace (nessuno aperto)"));
  }
}

void MainWindow::updateStatusCounts() {
  if(status_counts_ == nullptr) {
    return;
  }
  auto* panel = activePanel();
  status_counts_->setText(panel ? panel->countsText() : QStringLiteral("Nessuna area aperta"));
}

void MainWindow::setupWorkspaceColumnsMenu(QMenu* parent_menu) {
  auto* columns_menu = parent_menu->addMenu(QStringLiteral("Colonne workspace"));
  workspace_column_actions_.clear();
  workspace_column_actions_.reserve(kWorkspaceColumnCount);

  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  const QVariantList saved =
      settings.value(QString::fromUtf8(kWorkspaceColumnVisibleKey)).toList();

  for(int col = 0; col < kWorkspaceColumnCount; ++col) {
    auto* action = columns_menu->addAction(workspace_column_labels().at(col));
    action->setCheckable(true);
    action->setData(col);
    bool visible = true;
    if(col < saved.size()) {
      visible = saved.at(col).toBool();
    }
    action->setChecked(visible);
    workspace_column_actions_.push_back(action);
    connect(action, &QAction::toggled, this, [this, col](bool checked) {
      setWorkspaceColumnVisible(col, checked);
    });
  }
}

void MainWindow::setWorkspaceColumnVisible(int logical_column, bool visible) {
  if(workspace_table_ == nullptr || logical_column < 0 ||
     logical_column >= kWorkspaceColumnCount) {
    return;
  }
  if(!visible) {
    int visible_count = 0;
    for(int col = 0; col < kWorkspaceColumnCount; ++col) {
      if(!workspace_table_->isColumnHidden(col)) {
        ++visible_count;
      }
    }
    if(visible_count <= 1) {
      if(logical_column < static_cast<int>(workspace_column_actions_.size())) {
        const QSignalBlocker block(workspace_column_actions_[static_cast<std::size_t>(logical_column)]);
        workspace_column_actions_[static_cast<std::size_t>(logical_column)]->setChecked(true);
      }
      return;
    }
  }
  workspace_table_->setColumnHidden(logical_column, !visible);
  if(logical_column < static_cast<int>(workspace_column_actions_.size())) {
    const QSignalBlocker block(workspace_column_actions_[static_cast<std::size_t>(logical_column)]);
    workspace_column_actions_[static_cast<std::size_t>(logical_column)]->setChecked(visible);
  }
  saveWorkspaceTableState();
}

void MainWindow::applyWorkspaceColumnVisibility() {
  if(workspace_table_ == nullptr) {
    return;
  }
  for(std::size_t i = 0; i < workspace_column_actions_.size(); ++i) {
    const bool visible = workspace_column_actions_[i]->isChecked();
    workspace_table_->setColumnHidden(static_cast<int>(i), !visible);
  }
}

void MainWindow::saveWorkspaceTableState() {
  if(workspace_table_ == nullptr) {
    return;
  }
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  settings.setValue(QString::fromUtf8(kWorkspaceHeaderStateKey),
                    workspace_table_->header()->saveState());
  QVariantList visible;
  visible.reserve(kWorkspaceColumnCount);
  for(int col = 0; col < kWorkspaceColumnCount; ++col) {
    visible.push_back(!workspace_table_->isColumnHidden(col));
  }
  settings.setValue(QString::fromUtf8(kWorkspaceColumnVisibleKey), visible);
}

void MainWindow::restoreWorkspaceTableState() {
  if(workspace_table_ == nullptr) {
    return;
  }
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  const QByteArray state =
      settings.value(QString::fromUtf8(kWorkspaceHeaderStateKey)).toByteArray();
  if(!state.isEmpty()) {
    workspace_table_->header()->restoreState(state);
  }
  // Allinea di nuovo a sinistra: restoreState puo' ripristinare allineamenti vecchi
  workspace_table_->header()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  workspace_table_->header()->setSectionsMovable(true);
}

void MainWindow::refreshWorkspaceList() {
  if(workspace_table_ == nullptr) {
    return;
  }
  ensureProjectsLoaded();

  // Ricorda gruppi chiusi e selezione per non "saltare" a ogni aggiornamento.
  QSet<QString> collapsed;
  QStringList selected = workspace_table_->selectedAreaFolders();
  for(int i = 0; i < workspace_table_->topLevelItemCount(); ++i) {
    auto* g = workspace_table_->topLevelItem(i);
    if(!g->isExpanded()) {
      collapsed.insert(g->data(0, ws_tree::OrderRole).toString());
    }
  }

  const QSignalBlocker block(workspace_table_);
  workspace_table_->setSortingEnabled(false);
  workspace_table_->clear();

  QFont group_font = workspace_table_->font();
  group_font.setBold(true);
  QColor group_bg = palette().color(QPalette::Highlight);
  group_bg.setAlpha(70);

  auto make_group = [&](const QString& label, const QString& project, int kind,
                        const QString& order) {
    auto* g = new ws_tree::Item(workspace_table_);
    g->setText(0, label);
    g->setData(0, ws_tree::KindRole, kind);
    g->setData(0, ws_tree::ProjectRole, project);
    g->setData(0, ws_tree::OrderRole, order);
    g->setFlags((g->flags() | Qt::ItemIsDropEnabled) & ~Qt::ItemIsDragEnabled);
    for(int c = 0; c < kWorkspaceColumnCount; ++c) {
      g->setFont(c, group_font);
      g->setBackground(c, QBrush(group_bg));
    }
    g->setFirstColumnSpanned(true);
    return g;
  };

  QHash<QString, ws_tree::Item*> groups;
  const QStringList names = projects_->projectNames();
  for(int i = 0; i < names.size(); ++i) {
    auto* g = make_group(QString(), names[i], ws_tree::KindProject,
                         QStringLiteral("0_%1").arg(i, 4, 10, QLatin1Char('0')));
    g->setToolTip(0, QStringLiteral("Doppio clic: apre il pannello del progetto. "
                                    "Trascina qui le aree per aggiungerle."));
    groups.insert(names[i].toLower(), g);
  }
  auto* unassigned = make_group(QString(), QString(), ws_tree::KindUnassigned,
                                QStringLiteral("9_"));
  unassigned->setToolTip(0, QStringLiteral("Trascina qui un'area per toglierla dal progetto"));

  QHash<ws_tree::Item*, int> group_counts;
  for(const auto& area : workspace_areas_) {
    QString author;
    const QString zone_name =
        strip_zone_author(QString::fromStdString(area.zone_name), &author);
    const QString path = QString::fromStdString(area.path.string());
    const QString folder = QString::fromStdString(area.folder_name);
    const QString project = projects_->projectOf(folder);
    ws_tree::Item* parent = project.isEmpty() ? unassigned : groups.value(project.toLower(), unassigned);
    group_counts[parent] += 1;

    auto* item = new ws_tree::Item(parent);
    item->setData(0, ws_tree::KindRole, ws_tree::KindArea);
    item->setData(0, ws_tree::PathRole, path);
    item->setData(0, ws_tree::FolderRole, folder);
    item->setData(0, ws_tree::ProjectRole, project);
    item->setFlags((item->flags() | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
    auto set = [&](int col, const QString& text, const QVariant& sort, bool right = false) {
      item->setText(col, text);
      item->setData(col, ws_tree::SortRole, sort);
      item->setToolTip(col, QDir::toNativeSeparators(path));
      if(right) {
        item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
      }
    };
    set(0, folder, folder.toLower());
    if(area.zone_name.empty() && area.zone_num <= 0) {
      set(1, QStringLiteral("(manca %1.zon)").arg(folder), QString());
      item->setForeground(1, QBrush(QColor(0xE0, 0x8A, 0x2E)));
      item->setToolTip(1, QStringLiteral(
                              "Il file .zon deve avere lo stesso nome della cartella (es. %1/%1.zon)")
                              .arg(folder));
    } else {
      set(1, zone_name, zone_name.toLower());
    }
    set(2, author, author.toLower());
    set(3, area.zone_num > 0 ? QString::number(area.zone_num) : QString(),
        static_cast<qlonglong>(area.zone_num), true);
    set(4, area.top_vnum > 0 ? QString::number(area.top_vnum) : QString(),
        static_cast<qlonglong>(area.top_vnum), true);
    const int counts[] = {area.room_count, area.mob_count, area.obj_count};
    for(int c = 0; c < 3; ++c) {
      set(5 + c, counts[c] >= 0 ? QString::number(counts[c]) : QString(),
          static_cast<qlonglong>(counts[c]), true);
    }
    if(selected.contains(folder)) {
      item->setSelected(true);
    }
  }

  for(auto it = groups.begin(); it != groups.end(); ++it) {
    const QString name = it.value()->data(0, ws_tree::ProjectRole).toString();
    it.value()->setText(0, QStringLiteral("Progetto: %1   (%2 aree)")
                               .arg(name)
                               .arg(group_counts.value(it.value())));
  }
  unassigned->setText(0, QStringLiteral("Aree senza progetto   (%1)")
                             .arg(group_counts.value(unassigned)));

  for(int i = 0; i < workspace_table_->topLevelItemCount(); ++i) {
    auto* g = workspace_table_->topLevelItem(i);
    g->setExpanded(!collapsed.contains(g->data(0, ws_tree::OrderRole).toString()));
  }

  applyWorkspaceColumnVisibility();
  workspace_table_->setSortingEnabled(true);
  workspace_table_->sortByColumn(workspace_table_->header()->sortIndicatorSection() >= 0
                                     ? workspace_table_->header()->sortIndicatorSection()
                                     : 0,
                                 workspace_table_->header()->sortIndicatorOrder());

  if(workspace_dock_) {
    workspace_dock_->show();
  }
  updateTitle();
  refreshProjectPanels();
}

void MainWindow::loadWorkspace(const QString& path, bool interactive) {
  auto root = eleutheria::guess_workspace_root(path.toStdString());
  std::error_code ec;
  if(!std::filesystem::is_directory(root, ec)) {
    throw std::runtime_error("Cartella workspace non trovata:\n" + root.string());
  }
  workspace_ = eleutheria::make_workspace(root);
  workspace_areas_ = eleutheria::scan_workspace_areas(*workspace_);
  const QString root_q = QString::fromStdString(workspace_->root.string());
  saveLastWorkspacePath(root_q);
  eleutheria_ui::addKnownWorkspace(root_q);
  refreshWorkspaceList();
  rebuildWorkspaceIndex();
  updateTitle();
  if(workspace_areas_.empty()) {
    statusBar()->showMessage(
        QString("Workspace: %1  (vuoto) — File > Aggiungi area al workspace per inserire aree")
            .arg(root_q),
        15000);
  } else {
    statusBar()->showMessage(
        QString("Workspace: %1  (%2 aree) — doppio clic su un'area per aprirla")
            .arg(root_q)
            .arg(workspace_areas_.size()),
        10000);
  }
  const QString problems = areasWithNameProblems();
  if(!problems.isEmpty()) {
    if(interactive) {
      QMessageBox box(this);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(QStringLiteral("Aree senza nome"));
      box.setTextFormat(Qt::RichText);
      box.setText(
          QStringLiteral("Di alcune aree non riesco a leggere nome e numero di zona:<br><br>") +
          problems +
          QStringLiteral("<br>Il file .zon deve avere <b>lo stesso nome della cartella</b> "
                         "(es. <tt>B1/B1.zon</tt>). Rinomina il file e riapri il workspace."));
      box.exec();
    } else {
      statusBar()->showMessage(
          QStringLiteral("Attenzione: alcune aree non hanno un file .zon con il nome della "
                         "cartella (Gestione workspace > Apri per i dettagli)"),
          15000);
    }
  }
}

QString MainWindow::areasWithNameProblems() const {
  QString out;
  for(const auto& area : workspace_areas_) {
    if(!area.zone_name.empty() || area.zone_num > 0) {
      continue;
    }
    const QString folder = QString::fromStdString(area.folder_name);
    const QString dir = QString::fromStdString(area.path.string());
    const QStringList zons = QDir(dir).entryList({QStringLiteral("*.zon")}, QDir::Files);
    QString detail;
    if(zons.isEmpty()) {
      detail = QStringLiteral("manca il file <tt>%1.zon</tt>").arg(folder.toHtmlEscaped());
    } else {
      detail = QStringLiteral("trovato <tt>%1</tt>, dovrebbe chiamarsi <tt>%2.zon</tt>")
                   .arg(zons.join(QStringLiteral(", ")).toHtmlEscaped(),
                        folder.toHtmlEscaped());
    }
    out += QStringLiteral("&bull; <b>%1</b>: %2<br>").arg(folder.toHtmlEscaped(), detail);
  }
  return out;
}

void MainWindow::onWorkspaceAreaActivated(QTreeWidgetItem* item) {
  if(item == nullptr) {
    return;
  }
  const int kind = item->data(0, ws_tree::KindRole).toInt();
  if(kind == ws_tree::KindProject) {
    openProjectPanel(item->data(0, ws_tree::ProjectRole).toString());
    return;
  }
  if(kind == ws_tree::KindUnassigned) {
    item->setExpanded(!item->isExpanded());
    return;
  }
  const QString path = item->data(0, ws_tree::PathRole).toString();
  if(!path.isEmpty()) {
    openPath(path);
  }
}

void MainWindow::saveLastWorkspacePath(const QString& path) {
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  settings.setValue(QString::fromUtf8(kLastWorkspacePathKey), path);
}

void MainWindow::restoreLastWorkspace() {
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  const QString path = settings.value(QString::fromUtf8(kLastWorkspacePathKey)).toString();
  const bool first_launch = eleutheria_ui::knownWorkspaces().isEmpty();

  if(path.isEmpty()) {
    if(first_launch) {
      showWorkspaceManager(QString());
    }
    return;
  }
  if(!QFileInfo(path).isDir()) {
    showWorkspaceManager(
        QStringLiteral("L'ultimo workspace usato non esiste piu':<br><tt>%1</tt><br>"
                       "Forse la cartella e' stata spostata o rinominata. "
                       "Scegline un altro o aggiungilo di nuovo.")
            .arg(QDir::toNativeSeparators(path).toHtmlEscaped()));
    return;
  }
  try {
    loadWorkspace(path, false);
  } catch(const std::exception& e) {
    showWorkspaceManager(QString::fromUtf8(e.what()).toHtmlEscaped());
  }
}

void MainWindow::showWorkspaceManager(const QString& warning) {
  const QString current =
      workspace_ ? QString::fromStdString(workspace_->root.string()) : QString();
  eleutheria_ui::WorkspaceManagerDialog dialog(this, current, warning);
  if(dialog.exec() != QDialog::Accepted) {
    return;
  }
  switch(dialog.choice()) {
    case eleutheria_ui::WorkspaceManagerDialog::Choice::OpenWorkspace:
      try {
        loadWorkspace(dialog.chosenPath(), true);
      } catch(const std::exception& e) {
        QMessageBox::critical(this, "Errore workspace", QString::fromUtf8(e.what()));
      }
      break;
    case eleutheria_ui::WorkspaceManagerDialog::Choice::OpenSample:
      openPath(dialog.chosenPath());
      break;
    case eleutheria_ui::WorkspaceManagerDialog::Choice::None:
      break;
  }
}

void MainWindow::manageWorkspaces() {
  showWorkspaceManager(QString());
}

void MainWindow::closeEvent(QCloseEvent* event) {
  const auto docks = area_docks_;
  for(auto* dock : docks) {
    auto* panel = qobject_cast<AreaPanel*>(dock->widget());
    if(panel != nullptr && !panel->maybeSave()) {
      event->ignore();
      return;
    }
  }
  for(auto* dock : project_docks_) {
    if(auto* pp = qobject_cast<ProjectPanel*>(dock->widget())) {
      pp->flush();
    }
  }
  saveWorkspaceTableState();
  QSettings settings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
  settings.setValue(QString::fromUtf8(kWindowGeometryKey), saveGeometry());
  settings.setValue(QString::fromUtf8(kWindowStateKey), saveState(kWindowStateVersion));
  event->accept();
}

void MainWindow::newArea() {
  eleutheria::Area area;
  area.meta.name = "Nuova area";
  area.meta.author = "";
  area.meta.mud_id = "nebbie";
  area.dirty = false;
  addAreaPanel(std::move(area), QString());
}

void MainWindow::openPath(const QString& path) {
  if(path.isEmpty()) {
    return;
  }
  try {
    const std::filesystem::path fs_path(path.toStdString());
    // Se e' un workspace (cartella parent con molte aree), apri la lista aree.
    if(std::filesystem::is_directory(fs_path) && eleutheria::is_workspace_root(fs_path) &&
       !eleutheria::is_area_directory(fs_path)) {
      loadWorkspace(path);
      return;
    }
    // Cartella che non e' ne' workspace ne' area: spiega la regola.
    if(std::filesystem::is_directory(fs_path) && !eleutheria::is_area_directory(fs_path) &&
       !eleutheria::is_workspace_root(fs_path)) {
      const bool empty =
          QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
      QMessageBox box(this);
      box.setIcon(empty ? QMessageBox::Question : QMessageBox::Warning);
      box.setWindowTitle(QStringLiteral("Cartella senza aree"));
      box.setTextFormat(Qt::RichText);
      box.setText(
          QStringLiteral("In <tt>%1</tt> non ho trovato ne' un'area ne' un workspace.<br><br>")
              .arg(QDir::toNativeSeparators(path).toHtmlEscaped()) +
          eleutheria_ui::workspaceRuleText() +
          QStringLiteral("<br><br>Vuoi usare questa cartella come workspace (per ora vuoto)?"));
      box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
      box.setDefaultButton(empty ? QMessageBox::Yes : QMessageBox::No);
      if(box.exec() == QMessageBox::Yes) {
        loadWorkspace(path, true);
      }
      return;
    }

    const auto loaded = eleutheria::load_area(path.toStdString());
    const QString base = QString::fromStdString(loaded.area.base_path);

    // Gia' aperta? Porta in primo piano il suo pannello.
    if(auto* open = findOpenArea(base)) {
      if(auto* dock = qobject_cast<QDockWidget*>(open->parentWidget())) {
        dock->show();
        dock->raise();
      }
      setActivePanel(open);
      statusBar()->showMessage(QStringLiteral("Area gia' aperta: %1").arg(open->displayName()),
                               5000);
      return;
    }

    // Singola area senza workspace aperto: aggancia il workspace che la contiene.
    if(!workspace_ && std::filesystem::is_directory(fs_path) &&
       eleutheria::is_area_directory(fs_path)) {
      const auto ws_root = eleutheria::guess_workspace_root(fs_path);
      if(eleutheria::is_workspace_root(ws_root)) {
        workspace_ = eleutheria::make_workspace(ws_root);
        workspace_areas_ = eleutheria::scan_workspace_areas(*workspace_);
        refreshWorkspaceList();
        rebuildWorkspaceIndex();
      }
    }

    auto* panel = addAreaPanel(loaded.area, base);
    statusBar()->showMessage(QStringLiteral("Aperto: %1").arg(panel->countsText()), 8000);
    if(!loaded.warnings.empty()) {
      QString msg;
      for(const auto& w : loaded.warnings) {
        msg += QString::fromStdString(w) + "\n";
      }
      QMessageBox::warning(this, "Avvisi caricamento", msg);
    }
  } catch(const std::exception& e) {
    QMessageBox::critical(this, "Errore apertura", QString::fromUtf8(e.what()));
  }
}

void MainWindow::openAny() {
  QString start;
  if(auto* panel = activePanel(); panel != nullptr && !panel->basePath().isEmpty()) {
    start = QFileInfo(panel->basePath()).absolutePath();
  } else if(workspace_) {
    start = QString::fromStdString(workspace_->root.string());
  }
  const QString path = eleutheria_ui::chooseFileOrDirectory(
      this,
      QStringLiteral("Apri: un file area, una cartella area o un workspace"),
      QStringLiteral("Area Nebbie/Izanagi (*.ele *.wld *.mob *.obj *.zon *.shp);;Tutti (*)"),
      start);
  if(path.isEmpty()) {
    return;
  }
  openPath(path);
}

void MainWindow::addAreaToWorkspace() {
  if(!workspace_) {
    QMessageBox::information(
        this, QStringLiteral("Nessun workspace aperto"),
        QStringLiteral("Prima apri o crea un workspace (File > Gestione workspace)."));
    return;
  }
  const QString file = eleutheria_ui::chooseOpenFile(
      this, QStringLiteral("Scegli un file dell'area da aggiungere (verranno copiati tutti)"),
      QStringLiteral("Area Nebbie/Izanagi (*.ele *.wld *.mob *.obj *.zon *.shp);;Tutti (*)"));
  if(file.isEmpty()) {
    return;
  }
  const QFileInfo picked(file);
  const QString stem = picked.completeBaseName();
  const QDir src_dir = picked.absoluteDir();
  static const QStringList kExts = {QStringLiteral("zon"), QStringLiteral("wld"),
                                    QStringLiteral("mob"), QStringLiteral("obj"),
                                    QStringLiteral("shp"), QStringLiteral("ele")};
  QStringList sources;
  for(const auto& f : src_dir.entryList(QDir::Files)) {
    const QFileInfo fi(src_dir.filePath(f));
    if(fi.completeBaseName().compare(stem, Qt::CaseInsensitive) == 0 &&
       kExts.contains(fi.suffix().toLower())) {
      sources.append(fi.absoluteFilePath());
    }
  }
  if(sources.isEmpty()) {
    QMessageBox::warning(this, QStringLiteral("Nessun file area"),
                         QStringLiteral("Accanto a %1 non ho trovato file area "
                                        "(.zon .wld .mob .obj .shp .ele).")
                             .arg(picked.fileName()));
    return;
  }

  bool ok = false;
  const QString name =
      QInputDialog::getText(
          this, QStringLiteral("Aggiungi area al workspace"),
          QStringLiteral("Trovati %1 file: %2\n\nNome della cartella dell'area "
                         "(i file verranno rinominati uguali):")
              .arg(sources.size())
              .arg([&]() {
                QStringList n;
                for(const auto& s : sources) n << QFileInfo(s).fileName();
                return n.join(QStringLiteral(", "));
              }()),
          QLineEdit::Normal, stem, &ok)
          .trimmed();
  if(!ok || name.isEmpty()) {
    return;
  }
  if(name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
    QMessageBox::warning(this, QStringLiteral("Nome non valido"),
                         QStringLiteral("Il nome non puo' contenere / o \\."));
    return;
  }
  const QDir container(QString::fromStdString(workspace_->areas_container.string()));
  const QString target = container.filePath(name);
  if(QFileInfo::exists(target)) {
    QMessageBox::warning(this, QStringLiteral("Esiste gia'"),
                         QStringLiteral("Nel workspace c'e' gia' una cartella \"%1\".\n"
                                        "Scegli un altro nome.")
                             .arg(name));
    return;
  }
  if(!QDir().mkpath(target)) {
    QMessageBox::critical(this, QStringLiteral("Errore"),
                          QStringLiteral("Non riesco a creare la cartella:\n%1")
                              .arg(QDir::toNativeSeparators(target)));
    return;
  }
  for(const auto& s : sources) {
    const QString dest =
        QDir(target).filePath(name + QLatin1Char('.') + QFileInfo(s).suffix().toLower());
    if(!QFile::copy(s, dest)) {
      QMessageBox::critical(this, QStringLiteral("Errore copia"),
                            QStringLiteral("Copia non riuscita:\n%1\n->\n%2")
                                .arg(QDir::toNativeSeparators(s), QDir::toNativeSeparators(dest)));
      return;
    }
  }
  try {
    loadWorkspace(QString::fromStdString(workspace_->root.string()), true);
  } catch(const std::exception& e) {
    QMessageBox::critical(this, "Errore workspace", QString::fromUtf8(e.what()));
    return;
  }
  const auto ans = QMessageBox::question(
      this, QStringLiteral("Area aggiunta"),
      QStringLiteral("Area \"%1\" aggiunta al workspace (%2 file copiati).\n\nAprirla ora?")
          .arg(name)
          .arg(sources.size()));
  if(ans == QMessageBox::Yes) {
    openPath(target);
  }
}

void MainWindow::about() {
  QMessageBox::about(
      this, "Izanagi",
      "Izanagi — editor di aree per Nebbie Arcane\n"
      "Rifacimento cross-platform ispirato a Izanagi di Lavinio Cerquetti.\n"
      "Formato file allineato al server Nebbie Arcane (db.cpp).\n"
      "Compilabile su Windows, macOS e Linux (Qt 6 / C++17).");
}

void MainWindow::openSelectedWorkspaceArea() {
  if(workspace_table_ == nullptr) {
    return;
  }
  auto* item = workspace_table_->currentItem();
  if(item == nullptr) {
    statusBar()->showMessage(QStringLiteral("Seleziona prima un'area nell'elenco."), 4000);
    return;
  }
  onWorkspaceAreaActivated(item);
}

AreaPanel* MainWindow::addAreaPanel(eleutheria::Area area, const QString& base_path) {
  auto* panel = new AreaPanel(catalog_, std::move(area), base_path);
  panel->setMaxLineLength(max_line_length_);
  panel->setWorkspaceIndex(&workspace_index_, workspace_index_ready_);

  auto* dock = new QDockWidget(panel->displayName(), this);
  dock->setObjectName(QStringLiteral("areaDock_%1").arg(++area_dock_counter_));
  dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
  dock->setWidget(panel);
  dock->setToolTip(QDir::toNativeSeparators(base_path));
  dock->installEventFilter(this);

  placeContentDock(dock);
  area_docks_.push_back(dock);

  connect(panel, &AreaPanel::titleChanged, this, [this, panel, dock]() {
    dock->setWindowTitle(panel->displayName());
    dock->setToolTip(QDir::toNativeSeparators(panel->basePath()));
    if(panel == activePanel()) {
      updateTitle();
      updateStatusCounts();
    }
  });
  connect(panel, &AreaPanel::statusMessage, statusBar(), &QStatusBar::showMessage);
  connect(panel, &AreaPanel::closeRequested, this, [this, panel]() { closeAreaPanel(panel); });
  connect(dock, &QDockWidget::visibilityChanged, this, [this, panel](bool visible) {
    if(visible) {
      setActivePanel(panel);
    }
  });

  if(!area_toggle_action_->isChecked()) {
    area_toggle_action_->setChecked(true);
  }
  dock->show();
  dock->raise();
  updateAreaPlaceholder();
  // Dopo il layout: evita che il pannello workspace resti schiacciato.
  QTimer::singleShot(0, this, [this]() {
    if(workspace_dock_ != nullptr && workspace_dock_->isVisible() &&
       !workspace_dock_->isFloating() && workspace_dock_->width() < 520) {
      resizeDocks({workspace_dock_}, {620}, Qt::Horizontal);
    }
  });
  setActivePanel(panel);
  return panel;
}

AreaPanel* MainWindow::findOpenArea(const QString& base_path) const {
  if(base_path.isEmpty()) {
    return nullptr;
  }
  const QString wanted = QDir::cleanPath(QFileInfo(base_path).absoluteFilePath());
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  const auto cs = Qt::CaseInsensitive;
#else
  const auto cs = Qt::CaseSensitive;
#endif
  for(auto* dock : area_docks_) {
    auto* panel = qobject_cast<AreaPanel*>(dock->widget());
    if(panel == nullptr || panel->basePath().isEmpty()) {
      continue;
    }
    const QString have = QDir::cleanPath(QFileInfo(panel->basePath()).absoluteFilePath());
    if(have.compare(wanted, cs) == 0) {
      return panel;
    }
  }
  return nullptr;
}

bool MainWindow::closeAreaPanel(AreaPanel* panel) {
  if(panel == nullptr) {
    return true;
  }
  if(!panel->maybeSave()) {
    return false;
  }
  QDockWidget* dock = nullptr;
  for(auto it = area_docks_.begin(); it != area_docks_.end(); ++it) {
    if((*it)->widget() == panel) {
      dock = *it;
      area_docks_.erase(it);
      break;
    }
  }
  if(dock != nullptr) {
    dock->removeEventFilter(this);
    removeDockWidget(dock);
    dock->deleteLater();
  }
  if(active_panel_ == panel) {
    active_panel_ = nullptr;
    // Attiva un'altra area ancora visibile, se c'e'
    for(auto* other : area_docks_) {
      if(!other->visibleRegion().isEmpty() || other->isFloating()) {
        active_panel_ = qobject_cast<AreaPanel*>(other->widget());
        break;
      }
    }
    if(active_panel_ == nullptr && !area_docks_.empty()) {
      active_panel_ = qobject_cast<AreaPanel*>(area_docks_.back()->widget());
    }
  }
  updateAreaPlaceholder();
  updateTitle();
  updateStatusCounts();
  return true;
}

void MainWindow::setActivePanel(AreaPanel* panel) {
  if(panel == active_panel_) {
    return;
  }
  active_panel_ = panel;
  updateTitle();
  updateStatusCounts();
}

AreaPanel* MainWindow::activePanel() const {
  if(active_panel_ != nullptr) {
    return active_panel_;
  }
  if(!area_docks_.empty()) {
    return qobject_cast<AreaPanel*>(area_docks_.back()->widget());
  }
  return nullptr;
}

void MainWindow::updateAreaPlaceholder() {
  if(area_placeholder_dock_ == nullptr) {
    return;
  }
  const bool show = area_docks_.empty() && project_docks_.empty() &&
                    area_toggle_action_ != nullptr &&
                    area_toggle_action_->isChecked();
  area_placeholder_dock_->setVisible(show);
}

void MainWindow::setAreaWindowsVisible(bool visible) {
  for(auto* dock : area_docks_) {
    dock->setVisible(visible);
  }
  for(auto* dock : project_docks_) {
    dock->setVisible(visible);
  }
  updateAreaPlaceholder();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // La X di un pannello area chiude l'area (chiedendo se salvare), non la nasconde.
  if(event->type() == QEvent::Close) {
    for(auto* dock : area_docks_) {
      if(dock == watched) {
        event->ignore();
        closeAreaPanel(qobject_cast<AreaPanel*>(dock->widget()));
        return true;
      }
    }
    for(auto* dock : project_docks_) {
      if(dock == watched) {
        event->ignore();
        closeProjectPanel(qobject_cast<ProjectPanel*>(dock->widget()));
        return true;
      }
    }
  }
  // Clic dentro un pannello area: diventa l'area attiva (per i comandi dei menu).
  if(event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn) {
    if(auto* w = qobject_cast<QWidget*>(watched)) {
      for(QWidget* p = w; p != nullptr; p = p->parentWidget()) {
        if(auto* panel = qobject_cast<AreaPanel*>(p)) {
          setActivePanel(panel);
          break;
        }
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::saveActiveArea() {
  if(auto* panel = activePanel()) {
    panel->save();
  }
}

void MainWindow::saveActiveAreaAs() {
  if(auto* panel = activePanel()) {
    panel->saveAs();
  }
}

void MainWindow::saveAllAreas() {
  int saved = 0;
  for(auto* dock : area_docks_) {
    auto* panel = qobject_cast<AreaPanel*>(dock->widget());
    if(panel != nullptr && panel->isDirty() && panel->save()) {
      ++saved;
    }
  }
  statusBar()->showMessage(QStringLiteral("Aree salvate: %1").arg(saved), 4000);
}

void MainWindow::closeActiveArea() {
  closeAreaPanel(activePanel());
}

void MainWindow::validateActiveArea() {
  if(auto* panel = activePanel()) {
    panel->validate();
  } else {
    statusBar()->showMessage(QStringLiteral("Nessuna area aperta da validare."), 4000);
  }
}

// ---------------------------------------------------------------------------
// Pannelli di contenuto (aree e progetti) e progetti
// ---------------------------------------------------------------------------

void MainWindow::placeContentDock(QDockWidget* dock) {
  // Si incastra a schede con gli altri pannelli di contenuto, o al posto del segnaposto.
  QDockWidget* anchor = nullptr;
  if(!area_docks_.empty()) {
    anchor = area_docks_.back();
  } else if(!project_docks_.empty()) {
    anchor = project_docks_.back();
  } else {
    anchor = area_placeholder_dock_;
  }
  if(anchor != nullptr && !anchor->isFloating() &&
     dockWidgetArea(anchor) != Qt::NoDockWidgetArea) {
    addDockWidget(dockWidgetArea(anchor), dock);
    tabifyDockWidget(anchor, dock);
  } else {
    addDockWidget(Qt::RightDockWidgetArea, dock);
  }
}

void MainWindow::ensureProjectsLoaded() {
  const QString root = workspace_ ? QString::fromStdString(workspace_->root.string()) : QString();
  if(root == projects_root_) {
    return;
  }
  // Cambio workspace: i pannelli progetto del vecchio non valgono piu'.
  const auto docks = project_docks_;
  for(auto* dock : docks) {
    closeProjectPanel(qobject_cast<ProjectPanel*>(dock->widget()));
  }
  projects_root_ = root;
  if(root.isEmpty()) {
    projects_->load(QString());
    return;
  }
  QString error;
  if(!projects_->load(root, &error)) {
    QMessageBox::warning(this, QStringLiteral("Progetti"),
                         QStringLiteral("Non riesco a leggere %1:\n%2")
                             .arg(ProjectStore::fileName(), error));
  }
}

QString MainWindow::askNewProjectName(const QString& initial) {
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, QStringLiteral("Progetto"),
                            QStringLiteral("Nome del progetto:"), QLineEdit::Normal, initial, &ok)
          .trimmed();
  if(!ok || name.isEmpty()) {
    return {};
  }
  if(projects_->find(name) != nullptr && name.compare(initial, Qt::CaseInsensitive) != 0) {
    QMessageBox::warning(this, QStringLiteral("Progetto"),
                         QStringLiteral("Esiste gia' un progetto \"%1\".").arg(name));
    return {};
  }
  return name;
}

void MainWindow::newProject() {
  if(!workspace_) {
    QMessageBox::information(this, QStringLiteral("Progetti"),
                             QStringLiteral("Prima apri un workspace."));
    return;
  }
  ensureProjectsLoaded();
  const QString name = askNewProjectName();
  if(name.isEmpty()) {
    return;
  }
  projects_->createProject(name);
  // Se erano selezionate delle aree, le mette subito nel nuovo progetto.
  const QStringList selected = workspace_table_->selectedAreaFolders();
  if(!selected.isEmpty()) {
    projects_->assignAreas(selected, name);
  }
  refreshWorkspaceList();
}

void MainWindow::assignAreasToProject(const QStringList& folders, const QString& project) {
  if(folders.isEmpty()) {
    return;
  }
  projects_->assignAreas(folders, project);
  refreshWorkspaceList();
  statusBar()->showMessage(
      project.isEmpty()
          ? QStringLiteral("%1 aree tolte dal progetto").arg(folders.size())
          : QStringLiteral("%1 aree nel progetto \"%2\"").arg(folders.size()).arg(project),
      4000);
}

void MainWindow::showWorkspaceContextMenu(const QPoint& pos) {
  if(!workspace_) {
    return;
  }
  ensureProjectsLoaded();
  QTreeWidgetItem* item = workspace_table_->itemAt(pos);
  const int kind = item ? item->data(0, ws_tree::KindRole).toInt() : 0;
  QMenu menu(this);

  if(kind == ws_tree::KindArea) {
    if(!item->isSelected()) {
      workspace_table_->clearSelection();
      item->setSelected(true);
    }
    const QStringList folders = workspace_table_->selectedAreaFolders();
    menu.addAction(QStringLiteral("Apri area"), this, [this, item]() {
      onWorkspaceAreaActivated(item);
    });
    menu.addSeparator();
    auto* assign = menu.addMenu(folders.size() > 1
                                    ? QStringLiteral("Assegna %1 aree a progetto").arg(folders.size())
                                    : QStringLiteral("Assegna a progetto"));
    const QString current = item->data(0, ws_tree::ProjectRole).toString();
    for(const auto& name : projects_->projectNames()) {
      auto* a = assign->addAction(name, this, [this, folders, name]() {
        assignAreasToProject(folders, name);
      });
      if(folders.size() == 1 && name.compare(current, Qt::CaseInsensitive) == 0) {
        a->setCheckable(true);
        a->setChecked(true);
      }
    }
    if(!projects_->projectNames().isEmpty()) {
      assign->addSeparator();
    }
    assign->addAction(QStringLiteral("Nuovo progetto..."), this, [this, folders]() {
      const QString name = askNewProjectName();
      if(!name.isEmpty()) {
        projects_->createProject(name);
        assignAreasToProject(folders, name);
      }
    });
    if(!current.isEmpty() || folders.size() > 1) {
      menu.addAction(QStringLiteral("Togli dal progetto"), this, [this, folders]() {
        assignAreasToProject(folders, QString());
      });
    }
  } else if(kind == ws_tree::KindProject) {
    const QString name = item->data(0, ws_tree::ProjectRole).toString();
    menu.addAction(QStringLiteral("Apri progetto"), this, [this, name]() {
      openProjectPanel(name);
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Rinomina progetto..."), this, [this, name]() {
      const QString new_name = askNewProjectName(name);
      if(new_name.isEmpty() || new_name == name) {
        return;
      }
      if(auto* panel = findProjectPanel(name)) {
        panel->flush();
      }
      projects_->renameProject(name, new_name);
      if(auto* panel = findProjectPanel(name)) {
        panel->setProjectName(new_name);
        if(auto* dock = qobject_cast<QDockWidget*>(panel->parentWidget())) {
          dock->setWindowTitle(QStringLiteral("Progetto: %1").arg(new_name));
        }
      }
      refreshWorkspaceList();
    });
    menu.addAction(QStringLiteral("Elimina progetto..."), this, [this, name]() {
      const auto ans = QMessageBox::question(
          this, QStringLiteral("Elimina progetto"),
          QStringLiteral("Eliminare il progetto \"%1\"?\n\nLe sue aree NON vengono toccate: "
                         "finiscono in \"Aree senza progetto\". Si perdono descrizione, note "
                         "e to-do del progetto.")
              .arg(name));
      if(ans != QMessageBox::Yes) {
        return;
      }
      if(auto* panel = findProjectPanel(name)) {
        closeProjectPanel(panel);
      }
      projects_->deleteProject(name);
      refreshWorkspaceList();
    });
  } else {
    menu.addAction(QStringLiteral("Nuovo progetto..."), this, &MainWindow::newProject);
  }
  menu.exec(workspace_table_->viewport()->mapToGlobal(pos));
}

ProjectPanel* MainWindow::findProjectPanel(const QString& name) const {
  for(auto* dock : project_docks_) {
    auto* panel = qobject_cast<ProjectPanel*>(dock->widget());
    if(panel != nullptr && panel->projectName().compare(name, Qt::CaseInsensitive) == 0) {
      return panel;
    }
  }
  return nullptr;
}

void MainWindow::openProjectPanel(const QString& name) {
  if(name.isEmpty()) {
    return;
  }
  if(auto* open = findProjectPanel(name)) {
    if(auto* dock = qobject_cast<QDockWidget*>(open->parentWidget())) {
      dock->show();
      dock->raise();
    }
    return;
  }
  auto* panel = new ProjectPanel(projects_, name);
  auto* dock = new QDockWidget(QStringLiteral("Progetto: %1").arg(name), this);
  dock->setObjectName(QStringLiteral("projectDock_%1").arg(++area_dock_counter_));
  dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                    QDockWidget::DockWidgetFloatable);
  dock->setWidget(panel);
  placeContentDock(dock);
  project_docks_.push_back(dock);
  connect(panel, &ProjectPanel::openAreaRequested, this, &MainWindow::openPath);
  connect(panel, &ProjectPanel::closeRequested, this, [this, panel]() {
    closeProjectPanel(panel);
  });
  panel->refresh(workspace_areas_);
  if(!area_toggle_action_->isChecked()) {
    area_toggle_action_->setChecked(true);
  }
  dock->show();
  dock->raise();
  updateAreaPlaceholder();
}

void MainWindow::closeProjectPanel(ProjectPanel* panel) {
  if(panel == nullptr) {
    return;
  }
  panel->flush();
  for(auto it = project_docks_.begin(); it != project_docks_.end(); ++it) {
    if((*it)->widget() == panel) {
      QDockWidget* dock = *it;
      project_docks_.erase(it);
      removeDockWidget(dock);
      dock->deleteLater();
      break;
    }
  }
  updateAreaPlaceholder();
}

void MainWindow::refreshProjectPanels() {
  for(auto* dock : project_docks_) {
    if(auto* panel = qobject_cast<ProjectPanel*>(dock->widget())) {
      panel->refresh(workspace_areas_);
    }
  }
}
