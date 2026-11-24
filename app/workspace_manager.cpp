#include "workspace_manager.hpp"

#include "file_dialogs.hpp"

#include "eleutheria/workspace.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <filesystem>

namespace eleutheria_ui {
namespace {

constexpr const char* kKnownWorkspacesKey = "ui/knownWorkspaces";
constexpr int kPathRole = Qt::UserRole + 1;

QSettings app_settings() {
  return QSettings(QStringLiteral("NebbieArcane"), QStringLiteral("Izanagi"));
}

Qt::CaseSensitivity path_case() {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  return Qt::CaseInsensitive;
#else
  return Qt::CaseSensitive;
#endif
}

bool same_path(const QString& a, const QString& b) {
  return normalizeWorkspacePath(a).compare(normalizeWorkspacePath(b), path_case()) == 0;
}

std::filesystem::path to_fs(const QString& path) {
  return std::filesystem::path(path.toStdWString());
}

int count_areas(const QString& path) {
  try {
    const auto ws = eleutheria::make_workspace(to_fs(path));
    return static_cast<int>(eleutheria::scan_workspace_areas(ws).size());
  } catch(...) {
    return 0;
  }
}

bool directory_is_empty(const QString& path) {
  return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty();
}

}  // namespace

QString normalizeWorkspacePath(const QString& path) {
  if(path.isEmpty()) {
    return {};
  }
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList knownWorkspaces() {
  auto settings = app_settings();
  return settings.value(QString::fromUtf8(kKnownWorkspacesKey)).toStringList();
}

void addKnownWorkspace(const QString& path) {
  const QString norm = normalizeWorkspacePath(path);
  if(norm.isEmpty()) {
    return;
  }
  QStringList list = knownWorkspaces();
  for(const auto& p : list) {
    if(same_path(p, norm)) {
      return;
    }
  }
  list.append(norm);
  auto settings = app_settings();
  settings.setValue(QString::fromUtf8(kKnownWorkspacesKey), list);
}

void removeKnownWorkspace(const QString& path) {
  QStringList list = knownWorkspaces();
  QStringList out;
  for(const auto& p : list) {
    if(!same_path(p, path)) {
      out.append(p);
    }
  }
  auto settings = app_settings();
  settings.setValue(QString::fromUtf8(kKnownWorkspacesKey), out);
}

QString findSampleArea() {
  const QString app_dir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      app_dir + QStringLiteral("/sample/B1"),                         // Windows zip
      app_dir + QStringLiteral("/../share/izanagi/sample/B1"),        // Linux
      app_dir + QStringLiteral("/../Resources/sample/B1"),            // macOS bundle
      app_dir + QStringLiteral("/../sample/B1"),                      // build dir
      app_dir + QStringLiteral("/../../sample/B1"),
  };
  for(const auto& c : candidates) {
    if(QFileInfo(c + QStringLiteral("/B1.zon")).exists()) {
      return QDir::cleanPath(c);
    }
  }
  return {};
}

QString workspaceRuleText() {
  return QStringLiteral(
      "Un <b>workspace</b> e' una cartella che contiene le tue aree, "
      "<b>una sottocartella per ogni area</b>.<br>"
      "Dentro ogni sottocartella i file devono avere <b>lo stesso nome della cartella</b>, "
      "ad esempio:<br>"
      "<tt>&nbsp;&nbsp;Aree/B1/B1.zon, B1.wld, B1.mob, B1.obj</tt><br>"
      "Una cartella vuota va bene: puoi aggiungere le aree dopo "
      "(File &rarr; Aggiungi area al workspace).");
}

WorkspaceManagerDialog::WorkspaceManagerDialog(QWidget* parent, const QString& current_workspace,
                                               const QString& warning)
    : QDialog(parent), current_(normalizeWorkspacePath(current_workspace)) {
  setWindowTitle(QStringLiteral("Gestione workspace"));
  resize(760, 440);

  auto* root = new QVBoxLayout(this);

  intro_ = new QLabel(this);
  intro_->setWordWrap(true);
  intro_->setTextFormat(Qt::RichText);
  root->addWidget(intro_);

  if(!warning.isEmpty()) {
    auto* warn = new QLabel(warning, this);
    warn->setWordWrap(true);
    warn->setTextFormat(Qt::RichText);
    warn->setStyleSheet(QStringLiteral(
        "QLabel { background: #7a3b00; color: #ffffff; padding: 6px; border-radius: 4px; }"));
    root->addWidget(warn);
  }

  auto* body = new QHBoxLayout();
  root->addLayout(body, 1);

  list_ = new QTreeWidget(this);
  list_->setColumnCount(3);
  list_->setHeaderLabels({QStringLiteral("Nome"), QStringLiteral("Aree"),
                          QStringLiteral("Percorso")});
  list_->setRootIsDecorated(false);
  list_->setAlternatingRowColors(true);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->header()->setStretchLastSection(true);
  list_->header()->resizeSection(0, 180);
  list_->header()->resizeSection(1, 70);
  body->addWidget(list_, 1);

  auto* side = new QVBoxLayout();
  body->addLayout(side);
  open_btn_ = new QPushButton(QStringLiteral("Apri"), this);
  open_btn_->setDefault(true);
  auto* new_btn = new QPushButton(QStringLiteral("Nuovo..."), this);
  new_btn->setToolTip(QStringLiteral("Crea una nuova cartella workspace (anche vuota)"));
  auto* add_btn = new QPushButton(QStringLiteral("Aggiungi esistente..."), this);
  add_btn->setToolTip(QStringLiteral("Aggiungi all'elenco una cartella che hai gia'"));
  remove_btn_ = new QPushButton(QStringLiteral("Togli dall'elenco"), this);
  remove_btn_->setToolTip(
      QStringLiteral("Toglie il workspace dall'elenco. I file su disco NON vengono cancellati."));
  auto* sample_btn = new QPushButton(QStringLiteral("Apri l'area di esempio"), this);
  const QString sample = findSampleArea();
  sample_btn->setEnabled(!sample.isEmpty());
  sample_btn->setToolTip(sample.isEmpty() ? QStringLiteral("Area di esempio non trovata")
                                          : sample);
  side->addWidget(open_btn_);
  side->addSpacing(12);
  side->addWidget(new_btn);
  side->addWidget(add_btn);
  side->addWidget(remove_btn_);
  side->addSpacing(12);
  side->addWidget(sample_btn);
  side->addStretch(1);

  auto* buttons = new QDialogButtonBox(this);
  buttons->addButton(QStringLiteral("Chiudi"), QDialogButtonBox::RejectRole);
  root->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(open_btn_, &QPushButton::clicked, this, &WorkspaceManagerDialog::openSelected);
  connect(new_btn, &QPushButton::clicked, this, &WorkspaceManagerDialog::createNew);
  connect(add_btn, &QPushButton::clicked, this, &WorkspaceManagerDialog::addExisting);
  connect(remove_btn_, &QPushButton::clicked, this, &WorkspaceManagerDialog::removeSelected);
  connect(sample_btn, &QPushButton::clicked, this, [this, sample]() {
    choice_ = Choice::OpenSample;
    chosen_path_ = sample;
    accept();
  });
  connect(list_, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem*, int) { openSelected(); });
  connect(list_, &QTreeWidget::itemSelectionChanged, this,
          &WorkspaceManagerDialog::updateButtons);

  refresh(current_);
}

void WorkspaceManagerDialog::refresh(const QString& select_path) {
  const QStringList paths = knownWorkspaces();
  list_->clear();

  if(paths.isEmpty()) {
    intro_->setText(QStringLiteral("<h3>Benvenuto in Izanagi!</h3>") + workspaceRuleText() +
                    QStringLiteral("<br><br>Per iniziare: <b>Nuovo...</b> per creare un "
                                   "workspace, <b>Aggiungi esistente...</b> se hai gia' una "
                                   "cartella con le aree, oppure <b>Apri l'area di esempio</b>."));
  } else {
    intro_->setText(QStringLiteral(
        "Doppio clic su un workspace per aprirlo. "
        "<i>Togli dall'elenco</i> non cancella nulla dal disco."));
  }

  QTreeWidgetItem* to_select = nullptr;
  for(const auto& p : paths) {
    auto* item = new QTreeWidgetItem(list_);
    const QFileInfo info(p);
    item->setText(0, info.fileName().isEmpty() ? p : info.fileName());
    item->setText(2, QDir::toNativeSeparators(p));
    item->setToolTip(2, QDir::toNativeSeparators(p));
    item->setData(0, kPathRole, p);
    if(!info.isDir()) {
      item->setText(1, QStringLiteral("non trovata"));
      for(int c = 0; c < 3; ++c) {
        item->setForeground(c, QBrush(Qt::gray));
      }
    } else {
      item->setText(1, QString::number(count_areas(p)));
    }
    if(!current_.isEmpty() && same_path(p, current_)) {
      QFont f = item->font(0);
      f.setBold(true);
      item->setFont(0, f);
      item->setToolTip(0, QStringLiteral("Workspace aperto ora"));
    }
    if(!select_path.isEmpty() && same_path(p, select_path)) {
      to_select = item;
    }
  }
  if(to_select == nullptr && list_->topLevelItemCount() > 0) {
    to_select = list_->topLevelItem(0);
  }
  if(to_select != nullptr) {
    list_->setCurrentItem(to_select);
  }
  updateButtons();
}

QString WorkspaceManagerDialog::selectedPath() const {
  const auto* item = list_->currentItem();
  return item ? item->data(0, kPathRole).toString() : QString();
}

void WorkspaceManagerDialog::updateButtons() {
  const bool has = !selectedPath().isEmpty();
  open_btn_->setEnabled(has);
  remove_btn_->setEnabled(has);
}

void WorkspaceManagerDialog::openSelected() {
  const QString path = selectedPath();
  if(path.isEmpty()) {
    return;
  }
  if(!QFileInfo(path).isDir()) {
    const auto ans = QMessageBox::question(
        this, QStringLiteral("Workspace non trovato"),
        QStringLiteral("La cartella non esiste piu':\n%1\n\nToglierla dall'elenco?")
            .arg(QDir::toNativeSeparators(path)));
    if(ans == QMessageBox::Yes) {
      removeKnownWorkspace(path);
      refresh();
    }
    return;
  }
  choice_ = Choice::OpenWorkspace;
  chosen_path_ = path;
  accept();
}

void WorkspaceManagerDialog::createNew() {
  bool ok = false;
  const QString name =
      QInputDialog::getText(this, QStringLiteral("Nuovo workspace"),
                            QStringLiteral("Nome della cartella del nuovo workspace:"),
                            QLineEdit::Normal, QStringLiteral("Aree"), &ok)
          .trimmed();
  if(!ok || name.isEmpty()) {
    return;
  }
  if(name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
    QMessageBox::warning(this, QStringLiteral("Nome non valido"),
                         QStringLiteral("Il nome non puo' contenere / o \\."));
    return;
  }
  const QString parent_dir = chooseExistingDirectory(
      this, QStringLiteral("Dove creo la cartella \"%1\"?").arg(name));
  if(parent_dir.isEmpty()) {
    return;
  }
  const QString target = QDir(parent_dir).filePath(name);
  const QFileInfo info(target);
  if(info.exists()) {
    if(!info.isDir()) {
      QMessageBox::warning(this, QStringLiteral("Impossibile creare"),
                           QStringLiteral("Esiste gia' un file con questo nome:\n%1")
                               .arg(QDir::toNativeSeparators(target)));
      return;
    }
    const auto ans = QMessageBox::question(
        this, QStringLiteral("Cartella esistente"),
        QStringLiteral("La cartella esiste gia':\n%1\n\nUsarla come workspace?")
            .arg(QDir::toNativeSeparators(target)));
    if(ans != QMessageBox::Yes) {
      return;
    }
  } else if(!QDir().mkpath(target)) {
    QMessageBox::critical(this, QStringLiteral("Errore"),
                          QStringLiteral("Non riesco a creare la cartella:\n%1")
                              .arg(QDir::toNativeSeparators(target)));
    return;
  }
  addKnownWorkspace(target);
  refresh(target);
}

void WorkspaceManagerDialog::addExisting() {
  QString path = chooseExistingDirectory(
      this, QStringLiteral("Scegli la cartella workspace (quella che contiene le aree)"));
  if(path.isEmpty()) {
    return;
  }
  path = normalizeWorkspacePath(path);
  const auto fs_path = to_fs(path);

  if(eleutheria::is_area_directory(fs_path)) {
    const QString parent = normalizeWorkspacePath(QFileInfo(path).absolutePath());
    const auto ans = QMessageBox::question(
        this, QStringLiteral("Questa e' un'area"),
        QStringLiteral("<b>%1</b> e' una singola area, non un workspace.<br><br>"
                       "Vuoi aggiungere invece la cartella che la contiene?<br><tt>%2</tt>")
            .arg(QFileInfo(path).fileName().toHtmlEscaped(),
                 QDir::toNativeSeparators(parent).toHtmlEscaped()));
    if(ans != QMessageBox::Yes) {
      return;
    }
    path = parent;
  } else if(!eleutheria::is_workspace_root(fs_path) && !directory_is_empty(path)) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Nessuna area trovata"));
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("In <tt>%1</tt> non ho trovato aree.<br><br>")
                    .arg(QDir::toNativeSeparators(path).toHtmlEscaped()) +
                workspaceRuleText() +
                QStringLiteral("<br><br>Aggiungerla comunque all'elenco?"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if(box.exec() != QMessageBox::Yes) {
      return;
    }
  }
  addKnownWorkspace(path);
  refresh(path);
}

void WorkspaceManagerDialog::removeSelected() {
  const QString path = selectedPath();
  if(path.isEmpty()) {
    return;
  }
  const auto ans = QMessageBox::question(
      this, QStringLiteral("Togli dall'elenco"),
      QStringLiteral("Togliere dall'elenco questo workspace?\n%1\n\n"
                     "I file su disco NON vengono cancellati.")
          .arg(QDir::toNativeSeparators(path)));
  if(ans != QMessageBox::Yes) {
    return;
  }
  removeKnownWorkspace(path);
  refresh();
}

}  // namespace eleutheria_ui
