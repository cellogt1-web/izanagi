#include "project_panel.hpp"

#include "project_store.hpp"

#include <QAbstractItemView>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kAreaPathRole = Qt::UserRole + 1;

QTableWidgetItem* cell(const QString& text, bool right = false) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  if(right) {
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  }
  return item;
}

QString num(int v) {
  return v >= 0 ? QString::number(v) : QString();
}
}  // namespace

ProjectPanel::ProjectPanel(ProjectStore* store, const QString& project_name, QWidget* parent)
    : QWidget(parent), store_(store), name_(project_name) {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* tb = new QToolBar(this);
  tb->setObjectName(QStringLiteral("projectPanelToolbar"));
  tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
  tb->addAction(QStringLiteral("Apri area selezionata"), this, [this]() {
    const int row = areas_table_->currentRow();
    if(row < 0) {
      return;
    }
    const QString path = areas_table_->item(row, 0)->data(kAreaPathRole).toString();
    if(!path.isEmpty()) {
      emit openAreaRequested(path);
    }
  });
  tb->addSeparator();
  tb->addAction(QStringLiteral("Chiudi progetto"), this, &ProjectPanel::closeRequested);
  root->addWidget(tb);

  auto* body = new QWidget(this);
  auto* body_layout = new QVBoxLayout(body);
  body_layout->setContentsMargins(6, 6, 6, 6);
  root->addWidget(body, 1);

  summary_ = new QLabel(body);
  summary_->setTextFormat(Qt::RichText);
  body_layout->addWidget(summary_);

  auto* split = new QSplitter(Qt::Vertical, body);
  body_layout->addWidget(split, 1);

  // --- Aree del progetto ---
  areas_table_ = new QTableWidget(0, 8, split);
  areas_table_->setHorizontalHeaderLabels(
      {QStringLiteral("Cartella"), QStringLiteral("Nome"), QStringLiteral("Zona"),
       QStringLiteral("Top vnum"), QStringLiteral("Vnum usati"), QStringLiteral("Stanze"),
       QStringLiteral("Mob"), QStringLiteral("Oggetti")});
  areas_table_->verticalHeader()->setVisible(false);
  areas_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  areas_table_->setSelectionMode(QAbstractItemView::SingleSelection);
  areas_table_->setAlternatingRowColors(true);
  areas_table_->horizontalHeader()->setStretchLastSection(false);
  areas_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  areas_table_->setToolTip(QStringLiteral("Doppio clic per aprire l'area"));
  connect(areas_table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
    const QString path = areas_table_->item(row, 0)->data(kAreaPathRole).toString();
    if(!path.isEmpty()) {
      emit openAreaRequested(path);
    }
  });
  split->addWidget(areas_table_);

  // --- Descrizione / Note / To-do ---
  auto* bottom = new QSplitter(Qt::Horizontal, split);
  auto make_box = [&](const QString& title, QWidget* content) {
    auto* box = new QGroupBox(title, bottom);
    auto* l = new QVBoxLayout(box);
    l->addWidget(content);
    return box;
  };
  description_ = new QPlainTextEdit;
  description_->setPlaceholderText(QStringLiteral("Di cosa parla il progetto, ambientazione, livelli..."));
  notes_ = new QPlainTextEdit;
  notes_->setPlaceholderText(QStringLiteral("Appunti liberi"));
  bottom->addWidget(make_box(QStringLiteral("Descrizione"), description_));
  bottom->addWidget(make_box(QStringLiteral("Note"), notes_));

  auto* todo_box = new QGroupBox(QStringLiteral("To-do"), bottom);
  auto* todo_layout = new QVBoxLayout(todo_box);
  todo_list_ = new QListWidget(todo_box);
  todo_list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  todo_list_->setDragDropMode(QAbstractItemView::InternalMove);
  todo_list_->setToolTip(QStringLiteral("Spunta le cose fatte; trascina per riordinare; "
                                        "doppio clic per modificare il testo"));
  todo_layout->addWidget(todo_list_, 1);
  todo_input_ = new QLineEdit(todo_box);
  todo_input_->setPlaceholderText(QStringLiteral("Nuova cosa da fare... (Invio per aggiungere)"));
  todo_layout->addWidget(todo_input_);
  auto* todo_row = new QHBoxLayout();
  auto* add_btn = new QPushButton(QStringLiteral("Aggiungi"), todo_box);
  auto* del_btn = new QPushButton(QStringLiteral("Rimuovi selezionati"), todo_box);
  todo_row->addWidget(add_btn);
  todo_row->addWidget(del_btn);
  todo_row->addStretch(1);
  todo_layout->addLayout(todo_row);
  bottom->addWidget(todo_box);
  split->addWidget(bottom);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 1);

  save_timer_ = new QTimer(this);
  save_timer_->setSingleShot(true);
  save_timer_->setInterval(800);
  connect(save_timer_, &QTimer::timeout, this, &ProjectPanel::saveNow);

  connect(description_, &QPlainTextEdit::textChanged, this, &ProjectPanel::scheduleSave);
  connect(notes_, &QPlainTextEdit::textChanged, this, &ProjectPanel::scheduleSave);
  connect(todo_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
    if(loading_) {
      return;
    }
    QFont f = item->font();
    f.setStrikeOut(item->checkState() == Qt::Checked);
    const bool was = loading_;
    loading_ = true;
    item->setFont(f);
    loading_ = was;
    scheduleSave();
  });
  connect(todo_list_->model(), &QAbstractItemModel::rowsMoved, this,
          &ProjectPanel::scheduleSave);
  connect(todo_input_, &QLineEdit::returnPressed, this, &ProjectPanel::addTodo);
  connect(add_btn, &QPushButton::clicked, this, &ProjectPanel::addTodo);
  connect(del_btn, &QPushButton::clicked, this, &ProjectPanel::removeSelectedTodos);

  loadTexts();
}

void ProjectPanel::setProjectName(const QString& name) {
  flush();
  name_ = name;
}

void ProjectPanel::loadTexts() {
  const Project* p = store_->find(name_);
  loading_ = true;
  description_->setPlainText(p ? p->description : QString());
  notes_->setPlainText(p ? p->notes : QString());
  todo_list_->clear();
  if(p != nullptr) {
    for(const auto& t : p->todos) {
      auto* item = new QListWidgetItem(t.text, todo_list_);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable |
                     Qt::ItemIsDragEnabled);
      item->setCheckState(t.done ? Qt::Checked : Qt::Unchecked);
      QFont f = item->font();
      f.setStrikeOut(t.done);
      item->setFont(f);
    }
  }
  loading_ = false;
}

void ProjectPanel::scheduleSave() {
  if(!loading_) {
    save_timer_->start();
  }
}

void ProjectPanel::flush() {
  if(save_timer_->isActive()) {
    save_timer_->stop();
    saveNow();
  }
}

void ProjectPanel::saveNow() {
  const Project* current = store_->find(name_);
  if(current == nullptr) {
    return;
  }
  Project p = *current;
  p.description = description_->toPlainText();
  p.notes = notes_->toPlainText();
  p.todos.clear();
  for(int i = 0; i < todo_list_->count(); ++i) {
    const auto* item = todo_list_->item(i);
    if(!item->text().trimmed().isEmpty()) {
      p.todos.append({item->text(), item->checkState() == Qt::Checked});
    }
  }
  store_->updateProject(p);
  refresh(areas_cache_);  // aggiorna i totali (es. to-do aperti)
}

void ProjectPanel::addTodo() {
  const QString text = todo_input_->text().trimmed();
  if(text.isEmpty()) {
    return;
  }
  loading_ = true;
  auto* item = new QListWidgetItem(text, todo_list_);
  item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable |
                 Qt::ItemIsDragEnabled);
  item->setCheckState(Qt::Unchecked);
  loading_ = false;
  todo_input_->clear();
  saveNow();
}

void ProjectPanel::removeSelectedTodos() {
  const auto items = todo_list_->selectedItems();
  if(items.isEmpty()) {
    return;
  }
  for(auto* item : items) {
    delete item;
  }
  saveNow();
}

void ProjectPanel::refresh(const std::vector<eleutheria::WorkspaceArea>& areas) {
  if(&areas != &areas_cache_) {
    areas_cache_ = areas;
  }
  const Project* p = store_->find(name_);
  const QStringList folders = p ? p->areas : QStringList();

  areas_table_->setRowCount(0);
  int tot_rooms = 0;
  int tot_mobs = 0;
  int tot_objs = 0;
  int missing = 0;
  int row = 0;
  QStringList sorted = folders;
  sorted.sort(Qt::CaseInsensitive);
  for(const auto& folder : sorted) {
    const auto it = std::find_if(areas.begin(), areas.end(), [&](const auto& a) {
      return QString::fromStdString(a.folder_name).compare(folder, Qt::CaseInsensitive) == 0;
    });
    areas_table_->insertRow(row);
    auto* first = cell(folder);
    if(it == areas.end()) {
      ++missing;
      first->setToolTip(QStringLiteral("Cartella non presente nel workspace"));
      areas_table_->setItem(row, 0, first);
      auto* miss = cell(QStringLiteral("(non trovata nel workspace)"));
      miss->setForeground(QBrush(Qt::gray));
      areas_table_->setItem(row, 1, miss);
      ++row;
      continue;
    }
    const auto& a = *it;
    first->setData(kAreaPathRole, QString::fromStdString(a.path.string()));
    areas_table_->setItem(row, 0, first);
    QString name = QString::fromStdString(a.zone_name);
    const int br = name.lastIndexOf(QLatin1Char('['));
    if(br > 0) {
      name = name.left(br).trimmed();
    }
    areas_table_->setItem(row, 1, cell(name));
    areas_table_->setItem(row, 2, cell(a.zone_num > 0 ? QString::number(a.zone_num) : QString(), true));
    areas_table_->setItem(row, 3, cell(a.top_vnum > 0 ? QString::number(a.top_vnum) : QString(), true));
    areas_table_->setItem(
        row, 4,
        cell(a.used_vnum_max > 0
                 ? QStringLiteral("%1 – %2").arg(a.used_vnum_min).arg(a.used_vnum_max)
                 : QString()));
    areas_table_->setItem(row, 5, cell(num(a.room_count), true));
    areas_table_->setItem(row, 6, cell(num(a.mob_count), true));
    areas_table_->setItem(row, 7, cell(num(a.obj_count), true));
    tot_rooms += std::max(0, a.room_count);
    tot_mobs += std::max(0, a.mob_count);
    tot_objs += std::max(0, a.obj_count);
    ++row;
  }
  areas_table_->resizeColumnsToContents();
  areas_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

  int todo_open = 0;
  if(p != nullptr) {
    for(const auto& t : p->todos) {
      todo_open += t.done ? 0 : 1;
    }
  }
  QString text = QStringLiteral(
                     "<span style='font-size:15px'><b>%1</b></span>&nbsp;&nbsp;&nbsp;"
                     "Aree: <b>%2</b> &nbsp;·&nbsp; Stanze: <b>%3</b> &nbsp;·&nbsp; "
                     "Mob: <b>%4</b> &nbsp;·&nbsp; Oggetti: <b>%5</b> &nbsp;·&nbsp; "
                     "To-do aperti: <b>%6</b>")
                     .arg(name_.toHtmlEscaped())
                     .arg(folders.size())
                     .arg(tot_rooms)
                     .arg(tot_mobs)
                     .arg(tot_objs)
                     .arg(todo_open);
  if(missing > 0) {
    text += QStringLiteral(" &nbsp;·&nbsp; <span style='color:#e08a2e'>%1 non trovate</span>")
                .arg(missing);
  }
  summary_->setText(text);
}
