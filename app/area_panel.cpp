#include "area_panel.hpp"

#include "editors.hpp"
#include "file_dialogs.hpp"

#include "eleutheria/area_io.hpp"
#include "eleutheria/world.hpp"

#include <QAction>
#include <QFileInfo>
#include <QFont>
#include <QKeySequence>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
// Indici delle schede del pannello area (ordine di addTab in buildUi).
constexpr int kTabZone = 0;
constexpr int kTabRooms = 1;
constexpr int kTabMobs = 2;
constexpr int kTabObjs = 3;
}  // namespace

AreaPanel::AreaPanel(const eleutheria::Catalog& catalog, eleutheria::Area area,
                     const QString& base_path, QWidget* parent)
    : QWidget(parent), catalog_(catalog), area_(std::move(area)), base_path_(base_path) {
  area_.dirty = false;
  buildUi();
  room_editor_->setArea(&area_);
  spe_editor_->setArea(&area_);
  spe_editor_->load(area_.specials);
  applyWorkspaceIndexToEditors();
  refreshLists();
  // La scheda Zone e' la prima: mostra subito i dati generali dell'area.
  if(zone_list_->count() > 0) {
    zone_list_->setCurrentRow(0);
  }
}

QString AreaPanel::folderName() const {
  return base_path_.isEmpty() ? QString() : QFileInfo(base_path_).fileName();
}

QString AreaPanel::displayName() const {
  QString name = QString::fromStdString(area_.meta.name);
  if(name.isEmpty()) {
    name = folderName();
  }
  if(name.isEmpty()) {
    name = QStringLiteral("Nuova area");
  }
  if(area_.dirty) {
    name += QStringLiteral(" *");
  }
  return name;
}

bool AreaPanel::isDirty() const {
  return area_.dirty;
}

void AreaPanel::buildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Barra comandi del pannello: agisce solo su QUESTA area.
  toolbar_ = new QToolBar(this);
  toolbar_->setObjectName(QStringLiteral("areaPanelToolbar"));
  toolbar_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar_->addAction(QStringLiteral("Salva"), this, [this]() { save(); });
  toolbar_->addAction(QStringLiteral("Valida"), this, &AreaPanel::validate);
  toolbar_->addSeparator();
  toolbar_->addAction(QStringLiteral("+ Stanza"), this, &AreaPanel::addRoom);
  toolbar_->addAction(QStringLiteral("+ Mob"), this, &AreaPanel::addMob);
  toolbar_->addAction(QStringLiteral("+ Oggetto"), this, &AreaPanel::addObj);
  toolbar_->addAction(QStringLiteral("Elimina"), this, &AreaPanel::deleteCurrent);
  toolbar_->addSeparator();
  toolbar_->addAction(QStringLiteral("Chiudi area"), this, &AreaPanel::closeRequested);
  root->addWidget(toolbar_);

  tabs_ = new QTabWidget(this);
  root->addWidget(tabs_, 1);

  auto make_page = [&](QListWidget** list, QWidget* editor) {
    auto* page = new QWidget(this);
    auto* split = new QSplitter(Qt::Horizontal, page);
    *list = new QListWidget(split);
    (*list)->setMinimumWidth(240);
    auto* right = new QWidget(split);
    auto* right_layout = new QVBoxLayout(right);
    right_layout->setContentsMargins(4, 4, 4, 4);
    right_layout->addWidget(editor);
    split->addWidget(*list);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(split);
    return page;
  };

  room_editor_ = new RoomEditor(catalog_, this);
  mob_editor_ = new MobEditor(catalog_, this);
  obj_editor_ = new ObjEditor(catalog_, this);
  zone_editor_ = new ZoneEditor(this);
  spe_editor_ = new SpeEditor(this);

  connect(room_editor_, &RoomEditor::areaResetsChanged, this, [this]() {
    markDirty();
    if(current_room_) {
      area_.dirty_rooms.insert(*current_room_);
    }
    // I reset vivono nella zona: marca tutte le zone (di solito una)
    for(const auto& [num, _] : area_.zones) {
      area_.dirty_zones.insert(num);
    }
    if(current_zone_ && area_.zones.count(*current_zone_)) {
      zone_editor_->load(area_.zones.at(*current_zone_));
    }
  });
  connect(room_editor_, &RoomEditor::navigateRequested, this, [this](QChar kind, qint64 vnum) {
    applyCurrentEditor();
    if(kind == QLatin1Char('R') && area_.rooms.count(vnum)) {
      current_room_ = vnum;
      tabs_->setCurrentIndex(kTabRooms);
      refreshLists();
      room_editor_->setArea(&area_);
      room_editor_->load(area_.rooms.at(vnum));
    } else if(kind == QLatin1Char('M') && area_.mobiles.count(vnum)) {
      current_mob_ = vnum;
      tabs_->setCurrentIndex(kTabMobs);
      refreshLists();
      mob_editor_->load(area_.mobiles.at(vnum));
    } else if(kind == QLatin1Char('O') && area_.objects.count(vnum)) {
      current_obj_ = vnum;
      tabs_->setCurrentIndex(kTabObjs);
      refreshLists();
      obj_editor_->load(area_.objects.at(vnum));
    }
  });

  // Ordine: Zone (dati generali dell'area) per prima, poi i contenuti.
  tabs_->addTab(make_page(&zone_list_, zone_editor_), QStringLiteral("Zone"));
  tabs_->addTab(make_page(&room_list_, room_editor_), QStringLiteral("Stanze"));
  tabs_->addTab(make_page(&mob_list_, mob_editor_), QStringLiteral("Mob"));
  tabs_->addTab(make_page(&obj_list_, obj_editor_), QStringLiteral("Oggetti"));
  tabs_->addTab(spe_editor_, QStringLiteral("Special"));

  connect(room_list_, &QListWidget::currentRowChanged, this, &AreaPanel::onRoomSelected);
  connect(mob_list_, &QListWidget::currentRowChanged, this, &AreaPanel::onMobSelected);
  connect(obj_list_, &QListWidget::currentRowChanged, this, &AreaPanel::onObjSelected);
  connect(zone_list_, &QListWidget::currentRowChanged, this, &AreaPanel::onZoneSelected);

  connect(room_editor_, &RoomEditor::edited, this, [this]() {
    markDirty();
    if(current_room_) {
      area_.dirty_rooms.insert(*current_room_);
    }
  });
  connect(mob_editor_, &MobEditor::edited, this, [this]() {
    markDirty();
    if(current_mob_) {
      area_.dirty_mobs.insert(*current_mob_);
    }
  });
  connect(obj_editor_, &ObjEditor::edited, this, [this]() {
    markDirty();
    if(current_obj_) {
      area_.dirty_objects.insert(*current_obj_);
    }
  });
  connect(zone_editor_, &ZoneEditor::edited, this, [this]() {
    markDirty();
    if(current_zone_) {
      area_.dirty_zones.insert(*current_zone_);
    }
  });
  connect(spe_editor_, &SpeEditor::edited, this, &AreaPanel::onSpeEdited);
}

void AreaPanel::setWorkspaceIndex(eleutheria::WorkspaceEntityIndex* index, bool ready) {
  workspace_index_ = index;
  workspace_index_ready_ = ready && index != nullptr;
  applyWorkspaceIndexToEditors();
}

void AreaPanel::setMaxLineLength(int max_length) {
  room_editor_->setMaxLineLength(max_length);
  mob_editor_->setMaxLineLength(max_length);
  obj_editor_->setMaxLineLength(max_length);
}

void AreaPanel::applyWorkspaceIndexToEditors() {
  const QString folder = folderName();
  if(workspace_index_ready_) {
    workspace_index_->overlay_area(area_, folder.toStdString());
  }
  const eleutheria::WorkspaceEntityIndex* idx =
      workspace_index_ready_ ? workspace_index_ : nullptr;
  room_editor_->setWorkspaceIndex(idx, folder);
  spe_editor_->setWorkspaceIndex(idx, folder);
  spe_editor_->setArea(&area_);
}

QString AreaPanel::countsText() const {
  return QStringLiteral("%1 — Stanze: %2   Mob: %3   Oggetti: %4   Zone: %5   Shop: %6   Spe: %7")
      .arg(displayName())
      .arg(area_.rooms.size())
      .arg(area_.mobiles.size())
      .arg(area_.objects.size())
      .arg(area_.zones.size())
      .arg(area_.shops.size())
      .arg(area_.specials.size());
}

void AreaPanel::refreshLists() {
  const QSignalBlocker block_rooms(room_list_);
  const QSignalBlocker block_mobs(mob_list_);
  const QSignalBlocker block_objs(obj_list_);
  const QSignalBlocker block_zones(zone_list_);

  const QFont mono(QStringLiteral("Menlo"), 11);
  for(auto* list : {room_list_, mob_list_, obj_list_, zone_list_}) {
    list->setFont(mono);
    list->setUniformItemSizes(true);
    list->clear();
  }

  for(const auto& [vnum, room] : area_.rooms) {
    auto* item = new QListWidgetItem(QString::fromStdString(eleutheria::room_list_label(room)));
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qint64>(vnum)));
    item->setToolTip(QString::fromStdString(room.name));
    room_list_->addItem(item);
  }
  for(const auto& [vnum, mob] : area_.mobiles) {
    auto* item = new QListWidgetItem(QString::fromStdString(eleutheria::mob_list_label(mob)));
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qint64>(vnum)));
    mob_list_->addItem(item);
  }
  for(const auto& [vnum, obj] : area_.objects) {
    auto* item = new QListWidgetItem(QString::fromStdString(eleutheria::obj_list_label(obj)));
    item->setData(Qt::UserRole, QVariant::fromValue(static_cast<qint64>(vnum)));
    obj_list_->addItem(item);
  }
  for(const auto& [num, zone] : area_.zones) {
    auto* item = new QListWidgetItem(QString::fromStdString(eleutheria::zone_list_label(zone)));
    item->setData(Qt::UserRole, num);
    zone_list_->addItem(item);
  }

  auto select_by_id = [](QListWidget* list, qint64 id) {
    for(int i = 0; i < list->count(); ++i) {
      if(list->item(i)->data(Qt::UserRole).toLongLong() == id) {
        list->setCurrentRow(i);
        return;
      }
    }
  };
  if(current_room_) {
    select_by_id(room_list_, *current_room_);
  }
  if(current_mob_) {
    select_by_id(mob_list_, *current_mob_);
  }
  if(current_obj_) {
    select_by_id(obj_list_, *current_obj_);
  }
  if(current_zone_) {
    select_by_id(zone_list_, *current_zone_);
  }
  emit titleChanged();
}

void AreaPanel::markDirty() {
  const bool was = area_.dirty;
  area_.dirty = true;
  if(!was) {
    emit titleChanged();
  }
}

bool AreaPanel::save() {
  applyCurrentEditor();
  refreshLists();
  if(base_path_.isEmpty()) {
    return saveAs();
  }
  try {
    eleutheria::save_area(area_, base_path_.toStdString());
    area_.dirty = false;
    emit titleChanged();
    emit statusMessage(QStringLiteral("Area salvata: %1").arg(displayName()), 3000);
    return true;
  } catch(const std::exception& e) {
    QMessageBox::critical(this, QStringLiteral("Errore salvataggio"), QString::fromUtf8(e.what()));
    return false;
  }
}

bool AreaPanel::saveAs() {
  const QString suggested = QString::fromStdString(area_.meta.name);
  const QString start = base_path_.isEmpty() ? QString() : QFileInfo(base_path_).absolutePath();
  const QString path = eleutheria_ui::chooseSaveFile(
      this, QStringLiteral("Salva area come"), QStringLiteral("Area (*.ele)"), start,
      (suggested.isEmpty() ? QStringLiteral("area") : suggested) + QStringLiteral(".ele"));
  if(path.isEmpty()) {
    return false;
  }
  QString base = path;
  if(base.endsWith(QStringLiteral(".ele"), Qt::CaseInsensitive)) {
    base.chop(4);
  }
  base_path_ = base;
  return save();
}

bool AreaPanel::maybeSave() {
  applyCurrentEditor();
  if(!area_.dirty) {
    return true;
  }
  const auto ans = QMessageBox::question(
      this, QStringLiteral("Salvare?"),
      QStringLiteral("L'area \"%1\" ha modifiche non salvate. Salvare?")
          .arg(QString::fromStdString(area_.meta.name)),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
  if(ans == QMessageBox::Cancel) {
    return false;
  }
  if(ans == QMessageBox::Save) {
    return save();
  }
  return true;
}

void AreaPanel::validate() {
  applyCurrentEditor();
  applyWorkspaceIndexToEditors();
  const auto* idx = workspace_index_ready_ ? workspace_index_ : nullptr;
  const auto issues = eleutheria::validate_area(area_, idx);
  if(issues.empty()) {
    QMessageBox::information(this, QStringLiteral("Validazione"),
                             QStringLiteral("Nessun problema rilevato."));
    return;
  }
  QString msg;
  int infos = 0;
  int warnings = 0;
  for(const auto& issue : issues) {
    const char* sev =
        issue.severity == eleutheria::ValidationIssue::Severity::Info      ? "info"
        : issue.severity == eleutheria::ValidationIssue::Severity::Warning ? "warn"
                                                                           : "err";
    if(issue.severity == eleutheria::ValidationIssue::Severity::Info) {
      ++infos;
    } else {
      ++warnings;
    }
    msg += QString("[%1 %2 #%3] %4\n")
               .arg(QString::fromUtf8(sev))
               .arg(QString::fromStdString(issue.kind))
               .arg(issue.vnum)
               .arg(QString::fromStdString(issue.message));
  }
  if(warnings == 0) {
    QMessageBox::information(
        this, QStringLiteral("Validazione"),
        QStringLiteral("Solo avvisi informativi (%1), nessun errore bloccante.\n\n%2")
            .arg(infos)
            .arg(msg));
  } else {
    QMessageBox::warning(this, QStringLiteral("Validazione"), msg);
  }
}

void AreaPanel::applyCurrentEditor() {
  // Non chiamare refreshLists() qui: distruggerebbe gli item della lista
  // mentre e' ancora in corso currentRowChanged → crash.
  if(current_room_) {
    const eleutheria::Room* previous =
        area_.rooms.count(*current_room_) ? &area_.rooms.at(*current_room_) : nullptr;
    auto room = room_editor_->save(previous);
    if(room.vnum != *current_room_) {
      area_.rooms.erase(*current_room_);
      area_.dirty_rooms.erase(*current_room_);
      area_.dirty_rooms.insert(room.vnum);
      current_room_ = room.vnum;
    } else if(previous != nullptr) {
      eleutheria::preserve_flag_tokens(room, *previous);
    }
    area_.rooms[room.vnum] = room;
  }
  if(current_mob_) {
    const eleutheria::Mobile* previous =
        area_.mobiles.count(*current_mob_) ? &area_.mobiles.at(*current_mob_) : nullptr;
    auto mob = mob_editor_->save(previous);
    if(mob.vnum != *current_mob_) {
      area_.mobiles.erase(*current_mob_);
      area_.dirty_mobs.erase(*current_mob_);
      area_.dirty_mobs.insert(mob.vnum);
      current_mob_ = mob.vnum;
    } else if(previous != nullptr) {
      eleutheria::preserve_flag_tokens(mob, *previous);
    }
    area_.mobiles[mob.vnum] = mob;
  }
  if(current_obj_) {
    const eleutheria::GameObject* previous =
        area_.objects.count(*current_obj_) ? &area_.objects.at(*current_obj_) : nullptr;
    auto obj = obj_editor_->save(previous);
    if(obj.vnum != *current_obj_) {
      area_.objects.erase(*current_obj_);
      area_.dirty_objects.erase(*current_obj_);
      area_.dirty_objects.insert(obj.vnum);
      current_obj_ = obj.vnum;
    } else if(previous != nullptr) {
      eleutheria::preserve_flag_tokens(obj, *previous);
    }
    area_.objects[obj.vnum] = obj;
  }
  if(current_zone_) {
    auto zone = zone_editor_->save();
    if(zone.number != *current_zone_) {
      area_.zones.erase(*current_zone_);
      area_.dirty_zones.erase(*current_zone_);
      area_.dirty_zones.insert(zone.number);
      current_zone_ = zone.number;
    }
    area_.zones[zone.number] = zone;
  }
  area_.specials = spe_editor_->save();
}

void AreaPanel::onSpeEdited() {
  area_.specials = spe_editor_->save();
  area_.specials_dirty = true;
  markDirty();
  emit titleChanged();
}

void AreaPanel::onRoomSelected() {
  if(room_list_->currentItem() == nullptr) {
    return;
  }
  const auto vnum = room_list_->currentItem()->data(Qt::UserRole).toLongLong();
  applyCurrentEditor();
  const auto it = area_.rooms.find(vnum);
  if(it == area_.rooms.end()) {
    return;
  }
  current_room_ = vnum;
  room_editor_->setArea(&area_);
  room_editor_->load(it->second);
}

void AreaPanel::onMobSelected() {
  if(mob_list_->currentItem() == nullptr) {
    return;
  }
  const auto vnum = mob_list_->currentItem()->data(Qt::UserRole).toLongLong();
  applyCurrentEditor();
  const auto it = area_.mobiles.find(vnum);
  if(it == area_.mobiles.end()) {
    return;
  }
  current_mob_ = vnum;
  mob_editor_->load(it->second);
}

void AreaPanel::onObjSelected() {
  if(obj_list_->currentItem() == nullptr) {
    return;
  }
  const auto vnum = obj_list_->currentItem()->data(Qt::UserRole).toLongLong();
  applyCurrentEditor();
  const auto it = area_.objects.find(vnum);
  if(it == area_.objects.end()) {
    return;
  }
  current_obj_ = vnum;
  obj_editor_->load(it->second);
}

void AreaPanel::onZoneSelected() {
  if(zone_list_->currentItem() == nullptr) {
    return;
  }
  const int num = zone_list_->currentItem()->data(Qt::UserRole).toInt();
  applyCurrentEditor();
  const auto it = area_.zones.find(num);
  if(it == area_.zones.end()) {
    return;
  }
  current_zone_ = num;
  zone_editor_->load(it->second);
}

void AreaPanel::addRoom() {
  applyCurrentEditor();
  const std::int64_t vnum = area_.rooms.empty() ? 1 : (area_.rooms.rbegin()->first + 1);
  eleutheria::Room room;
  room.vnum = vnum;
  room.name = "Nuova stanza";
  room.description = "Descrizione.\n";
  area_.rooms[vnum] = room;
  area_.dirty_rooms.insert(vnum);
  current_room_ = vnum;
  markDirty();
  refreshLists();
  room_editor_->setArea(&area_);
  room_editor_->load(area_.rooms.at(vnum));
  tabs_->setCurrentIndex(kTabRooms);
}

void AreaPanel::addMob() {
  applyCurrentEditor();
  const std::int64_t vnum = area_.mobiles.empty() ? 1 : (area_.mobiles.rbegin()->first + 1);
  eleutheria::Mobile mob;
  mob.vnum = vnum;
  mob.name = "nuovo mob";
  mob.short_descr = "un nuovo mob";
  mob.long_descr = "Un nuovo mob e' qui.\n";
  mob.letter = 'A';
  mob.gold_exp_race_form = true;
  area_.mobiles[vnum] = mob;
  area_.dirty_mobs.insert(vnum);
  current_mob_ = vnum;
  markDirty();
  refreshLists();
  mob_editor_->load(area_.mobiles.at(vnum));
  tabs_->setCurrentIndex(kTabMobs);
}

void AreaPanel::addObj() {
  applyCurrentEditor();
  const std::int64_t vnum = area_.objects.empty() ? 1 : (area_.objects.rbegin()->first + 1);
  eleutheria::GameObject obj;
  obj.vnum = vnum;
  obj.name = "nuovo oggetto";
  obj.short_description = "un nuovo oggetto";
  obj.description = "Un nuovo oggetto e' qui.";
  obj.type_flag = 15;
  area_.objects[vnum] = obj;
  area_.dirty_objects.insert(vnum);
  current_obj_ = vnum;
  markDirty();
  refreshLists();
  obj_editor_->load(area_.objects.at(vnum));
  tabs_->setCurrentIndex(kTabObjs);
}

void AreaPanel::deleteCurrent() {
  const int tab = tabs_->currentIndex();
  if(tab == kTabRooms && current_room_) {
    area_.dirty_rooms.erase(*current_room_);
    area_.rooms.erase(*current_room_);
    current_room_.reset();
    room_editor_->clear();
  } else if(tab == kTabMobs && current_mob_) {
    area_.dirty_mobs.erase(*current_mob_);
    area_.mobiles.erase(*current_mob_);
    current_mob_.reset();
    mob_editor_->clear();
  } else if(tab == kTabObjs && current_obj_) {
    area_.dirty_objects.erase(*current_obj_);
    area_.objects.erase(*current_obj_);
    current_obj_.reset();
    obj_editor_->clear();
  } else if(tab == kTabZone && current_zone_) {
    area_.dirty_zones.erase(*current_zone_);
    area_.zones.erase(*current_zone_);
    current_zone_.reset();
    zone_editor_->clear();
  } else {
    return;
  }
  markDirty();
  refreshLists();
}
