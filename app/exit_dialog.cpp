#include "exit_dialog.hpp"

#include "eleutheria/world.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace exit_ui {
namespace {

constexpr std::int64_t kIsDoor = 1;
constexpr std::int64_t kClosed = 2;
constexpr std::int64_t kLocked = 4;
constexpr const char* kUnknownArea = "__unknown__";

QString room_label(const eleutheria::Room& r) {
  return QString::fromStdString(eleutheria::strip_mud_colors(r.name));
}

int zone_num_for_room(const eleutheria::Area& area, std::int64_t room) {
  // Zona il cui intervallo contiene la stanza; altrimenti la prima.
  int best = area.zones.empty() ? -1 : area.zones.begin()->first;
  std::int64_t best_top = -1;
  for(const auto& [num, z] : area.zones) {
    if(room <= z.top && (best_top < 0 || z.top < best_top)) {
      best = num;
      best_top = z.top;
    }
  }
  return best;
}

QString state_word(SpawnState s) {
  switch(s) {
    case SpawnState::Open: return QStringLiteral("aperta");
    case SpawnState::Closed: return QStringLiteral("chiusa");
    case SpawnState::Locked: return QStringLiteral("chiusa a chiave");
    default: return {};
  }
}

const char* kDirEnglish[] = {"north", "east", "south", "west", "up", "down"};

}  // namespace

QString dir_name(int dir) {
  static const char* names[] = {"nord", "est", "sud", "ovest", "su", "giu'"};
  return (dir >= 0 && dir < 6) ? QString::fromUtf8(names[dir]) : QString();
}

int opposite_dir(int dir) {
  static const int opp[] = {2, 3, 0, 1, 5, 4};
  return (dir >= 0 && dir < 6) ? opp[dir] : -1;
}

SpawnState find_spawn_state(const eleutheria::Area& area, std::int64_t room, int dir) {
  for(const auto& [num, zone] : area.zones) {
    for(const auto& cmd : zone.commands) {
      if(!cmd.is_comment && cmd.command == 'D' && cmd.arg1 == room && cmd.arg2 == dir) {
        const int st = static_cast<int>(cmd.arg3);
        if(st >= 0 && st <= 2) {
          return static_cast<SpawnState>(st);
        }
      }
    }
  }
  return SpawnState::NoReset;
}

bool apply_exit(eleutheria::Area& area, std::int64_t room, int dir, const eleutheria::Exit& exit,
                SpawnState state) {
  auto it = area.rooms.find(room);
  if(it == area.rooms.end() || dir < 0 || dir >= eleutheria::kMaxExits) {
    return false;
  }
  it->second.exits[static_cast<std::size_t>(dir)] = exit;
  area.dirty_rooms.insert(room);
  area.dirty = true;

  // Reset D: aggiorna quello esistente, lo aggiunge o lo toglie.
  bool found = false;
  for(auto& [num, zone] : area.zones) {
    auto& cmds = zone.commands;
    for(auto c = cmds.begin(); c != cmds.end();) {
      if(!c->is_comment && c->command == 'D' && c->arg1 == room && c->arg2 == dir) {
        if(found || state == SpawnState::NoReset || !exit.present) {
          c = cmds.erase(c);
          area.dirty_zones.insert(num);
          continue;
        }
        if(c->arg3 != static_cast<int>(state)) {
          c->arg3 = static_cast<int>(state);
          area.dirty_zones.insert(num);
        }
        found = true;
      }
      ++c;
    }
  }
  if(!found && state != SpawnState::NoReset && exit.present) {
    const int zone_num = zone_num_for_room(area, room);
    if(zone_num >= 0) {
      eleutheria::ResetCommand cmd;
      cmd.command = 'D';
      cmd.if_flag = 0;
      cmd.arg1 = room;
      cmd.arg2 = dir;
      cmd.arg3 = static_cast<int>(state);
      cmd.arg4 = 0;
      cmd.comment = QStringLiteral("  *! Uscita %1 della stanza %2 (%3): %4")
                        .arg(QString::fromUtf8(kDirEnglish[dir]))
                        .arg(room)
                        .arg(room_label(it->second))
                        .arg(state_word(state))
                        .toStdString();
      area.zones[zone_num].commands.push_back(cmd);
      area.dirty_zones.insert(zone_num);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------

ExitDialog::ExitDialog(QWidget* parent, const eleutheria::Catalog& catalog,
                       const eleutheria::Area* area, const QString& area_folder,
                       const eleutheria::WorkspaceEntityIndex* index, std::int64_t room_vnum,
                       const QString& room_name, int dir, const eleutheria::Exit& current,
                       SpawnState current_state)
    : QDialog(parent),
      catalog_(catalog),
      area_(area),
      area_folder_(area_folder),
      index_(index),
      room_vnum_(room_vnum),
      dir_(dir),
      original_(current) {
  setWindowTitle(current.present ? QStringLiteral("Modifica uscita") : QStringLiteral("Crea uscita"));
  resize(780, 800);
  auto* root = new QVBoxLayout(this);

  auto* head = new QLabel(
      QStringLiteral("<b>Uscita verso %1</b> dalla stanza <b>#%2</b> %3")
          .arg(dir_name(dir).toUpper(), QString::number(room_vnum), room_name.toHtmlEscaped()),
      this);
  root->addWidget(head);

  // --- Destinazione ---
  auto* dest_box = new QGroupBox(QStringLiteral("Destinazione"), this);
  auto* dest_layout = new QGridLayout(dest_box);
  dest_area_ = new QComboBox(dest_box);
  dest_area_->addItem(QStringLiteral("Questa area (%1)").arg(area_folder_.isEmpty()
                                                                 ? QStringLiteral("non salvata")
                                                                 : area_folder_),
                      area_folder_);
  if(index_ != nullptr) {
    QStringList others;
    for(const auto& [vnum, ent] : index_->rooms) {
      const QString a = QString::fromStdString(ent.area_name);
      if(!a.isEmpty() && a.compare(area_folder_, Qt::CaseInsensitive) != 0 && !others.contains(a)) {
        others << a;
      }
    }
    others.sort(Qt::CaseInsensitive);
    for(const auto& a : others) {
      dest_area_->addItem(a, a);
    }
  }
  dest_area_->addItem(QStringLiteral("Area sconosciuta (sul MUD ma non nel workspace)"),
                      QString::fromUtf8(kUnknownArea));
  room_filter_ = new QLineEdit(dest_box);
  room_filter_->setPlaceholderText(QStringLiteral("Filtra per vnum o nome..."));
  room_list_ = new QListWidget(dest_box);
  room_list_->setFont(QFont(QStringLiteral("Menlo"), 10));
  room_list_->setMinimumHeight(170);
  manual_vnum_ = new QLineEdit(dest_box);
  manual_vnum_->setPlaceholderText(QStringLiteral("vnum della stanza"));
  dest_label_ = new QLabel(dest_box);
  dest_layout->addWidget(new QLabel(QStringLiteral("Area"), dest_box), 0, 0);
  dest_layout->addWidget(dest_area_, 0, 1);
  dest_layout->addWidget(new QLabel(QStringLiteral("Stanza"), dest_box), 1, 0);
  dest_layout->addWidget(room_filter_, 1, 1);
  dest_layout->addWidget(room_list_, 2, 1);
  dest_layout->addWidget(new QLabel(QStringLiteral("Vnum"), dest_box), 3, 0);
  dest_layout->addWidget(manual_vnum_, 3, 1);
  dest_layout->addWidget(dest_label_, 4, 1);
  two_way_ = new QCheckBox(
      QStringLiteral("Doppio senso: crea anche l'uscita di ritorno (%1) nella stanza di destinazione")
          .arg(dir_name(opposite_dir(dir))),
      dest_box);
  dest_layout->addWidget(two_way_, 5, 0, 1, 2);
  root->addWidget(dest_box, 3);

  // --- Porta ---
  auto* door_box = new QGroupBox(QStringLiteral("Porta"), this);
  auto* door_form = new QFormLayout(door_box);
  door_type_ = new QComboBox(door_box);
  door_type_->addItem(QStringLiteral("Passaggio libero (nessuna porta)"), 0);
  door_type_->addItem(QStringLiteral("Porta senza chiave"), 1);
  door_type_->addItem(QStringLiteral("Porta con chiave"), 2);
  auto* key_row = new QHBoxLayout();
  key_vnum_ = new QLineEdit(door_box);
  key_vnum_->setPlaceholderText(QStringLiteral("vnum oggetto chiave"));
  key_label_ = new QLabel(door_box);
  key_row->addWidget(key_vnum_);
  key_row->addWidget(key_label_, 1);
  spawn_state_ = new QComboBox(door_box);
  spawn_state_->addItem(QStringLiteral("Nessun reset (resta com'e')"), -1);
  spawn_state_->addItem(QStringLiteral("Aperta"), 0);
  spawn_state_->addItem(QStringLiteral("Chiusa"), 1);
  spawn_state_->addItem(QStringLiteral("Chiusa a chiave"), 2);
  spawn_state_->setToolTip(QStringLiteral(
      "Stato della porta a ogni reset della zona (comando D nel file .zon)."));
  door_form->addRow(QStringLiteral("Tipo"), door_type_);
  door_form->addRow(QStringLiteral("Chiave"), key_row);
  door_form->addRow(QStringLiteral("Stato allo spawn"), spawn_state_);
  auto* flags_grid = new QGridLayout();
  int fi = 0;
  for(const auto& f : catalog_.exit_flags()) {
    if(f.bit_index <= 2) {
      continue;  // ISDOOR/CLOSED/LOCKED: gestiti da Tipo e Stato allo spawn
    }
    auto* cb = new QCheckBox(QString::fromStdString(f.label), door_box);
    cb->setProperty("bit", f.bit_index);
    cb->setChecked((current.exit_info & (std::int64_t{1} << f.bit_index)) != 0);
    flags_grid->addWidget(cb, fi / 3, fi % 3);
    flag_boxes_ << cb;
    ++fi;
  }
  door_form->addRow(QStringLiteral("Altri flag"), flags_grid);
  root->addWidget(door_box);

  // --- Testi ---
  auto* text_box = new QGroupBox(QStringLiteral("Testi"), this);
  auto* text_form = new QFormLayout(text_box);
  keywords_ = new QLineEdit(text_box);
  keywords_->setPlaceholderText(QStringLiteral("es. porta cancello (per open/close/unlock)"));
  description_ = new QPlainTextEdit(text_box);
  description_->setPlaceholderText(QStringLiteral("Cosa si vede guardando in questa direzione"));
  description_->setFont(QFont(QStringLiteral("Courier New"), 10));
  description_->setMaximumHeight(110);
  text_form->addRow(QStringLiteral("Parole chiave"), keywords_);
  text_form->addRow(QStringLiteral("Descrizione"), description_);
  root->addWidget(text_box);

  auto* buttons = new QDialogButtonBox(this);
  buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
  buttons->addButton(QStringLiteral("Annulla"), QDialogButtonBox::RejectRole);
  if(current.present) {
    auto* del = buttons->addButton(QStringLiteral("Elimina uscita"), QDialogButtonBox::DestructiveRole);
    connect(del, &QPushButton::clicked, this, [this]() {
      result_ = {};
      result_.remove = true;
      accept();
    });
  }
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &ExitDialog::acceptExit);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // --- Valori iniziali ---
  const bool is_door = (current.exit_info & kIsDoor) != 0;
  const bool has_key = current.key > 0;
  door_type_->setCurrentIndex(is_door ? (has_key ? 2 : 1) : 0);
  if(has_key) {
    key_vnum_->setText(QString::number(current.key));
  }
  spawn_state_->setCurrentIndex(std::max(0, spawn_state_->findData(static_cast<int>(current_state))));
  keywords_->setText(QString::fromStdString(current.keyword));
  description_->setPlainText(QString::fromStdString(current.general_description));

  // Area iniziale: quella che contiene la destinazione attuale.
  if(current.present && current.to_room > 0) {
    if(area_ != nullptr && area_->rooms.count(current.to_room)) {
      dest_area_->setCurrentIndex(0);
    } else if(const auto* ent = index_ ? index_->find_room(current.to_room) : nullptr) {
      dest_area_->setCurrentIndex(
          std::max(0, dest_area_->findData(QString::fromStdString(ent->area_name))));
    } else {
      dest_area_->setCurrentIndex(dest_area_->count() - 1);
    }
    manual_vnum_->setText(QString::number(current.to_room));
  }

  connect(dest_area_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
    fillRooms();
    updateEnabled();
  });
  connect(room_filter_, &QLineEdit::textChanged, this, &ExitDialog::fillRooms);
  connect(room_list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
    if(item != nullptr) {
      const QSignalBlocker b(manual_vnum_);
      manual_vnum_->setText(QString::number(item->data(Qt::UserRole).toLongLong()));
      updateEnabled();
    }
  });
  connect(room_list_, &QListWidget::itemDoubleClicked, this, [this]() { acceptExit(); });
  connect(manual_vnum_, &QLineEdit::textChanged, this, &ExitDialog::updateEnabled);
  connect(door_type_, qOverload<int>(&QComboBox::currentIndexChanged), this, &ExitDialog::updateEnabled);
  connect(key_vnum_, &QLineEdit::textChanged, this, &ExitDialog::updateKeyLabel);

  two_way_default_ = !current.present;  // nuova uscita: doppio senso proposto di default
  connect(two_way_, &QCheckBox::toggled, this, [this]() { two_way_touched_ = true; });
  fillRooms();
  updateEnabled();
  updateKeyLabel();
}

void ExitDialog::fillRooms() {
  const QString area = dest_area_->currentData().toString();
  const QString filter = room_filter_->text().trimmed();
  const QSignalBlocker b(room_list_);
  room_list_->clear();
  const bool unknown = area == QString::fromUtf8(kUnknownArea);
  room_list_->setEnabled(!unknown);
  room_filter_->setEnabled(!unknown);
  if(unknown) {
    return;
  }
  bool ok = false;
  const std::int64_t wanted = manual_vnum_->text().toLongLong(&ok);
  auto add = [&](std::int64_t vnum, const QString& name) {
    const QString text = QStringLiteral("%1  %2").arg(vnum, 6).arg(name);
    if(!filter.isEmpty() && !text.contains(filter, Qt::CaseInsensitive)) {
      return;
    }
    auto* item = new QListWidgetItem(text, room_list_);
    item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(vnum));
    if(ok && vnum == wanted) {
      room_list_->setCurrentItem(item);
    }
  };
  if(dest_area_->currentIndex() == 0 && area_ != nullptr) {
    for(const auto& [vnum, room] : area_->rooms) {
      add(vnum, room_label(room));
    }
  } else if(index_ != nullptr) {
    for(const auto& [vnum, ent] : index_->rooms) {
      if(QString::fromStdString(ent.area_name).compare(area, Qt::CaseInsensitive) == 0) {
        add(vnum, QString::fromStdString(ent.label));
      }
    }
  }
}

std::int64_t ExitDialog::selectedVnum(bool* ok) const {
  return manual_vnum_->text().trimmed().toLongLong(ok);
}

void ExitDialog::updateEnabled() {
  const bool door = door_type_->currentData().toInt() > 0;
  const bool keyed = door_type_->currentData().toInt() == 2;
  key_vnum_->setEnabled(keyed);
  spawn_state_->setEnabled(door);
  if(!door) {
    spawn_state_->setCurrentIndex(0);
  }
  for(auto* cb : flag_boxes_) {
    cb->setEnabled(door);
  }

  bool ok = false;
  const std::int64_t vnum = selectedVnum(&ok);
  const QString area = dest_area_->currentData().toString();
  const bool unknown = area == QString::fromUtf8(kUnknownArea);
  QString where;
  bool known = false;
  if(ok && vnum > 0) {
    if(area_ != nullptr && area_->rooms.count(vnum)) {
      where = QStringLiteral("#%1 %2 (questa area)").arg(vnum).arg(room_label(area_->rooms.at(vnum)));
      known = true;
    } else if(const auto* ent = index_ ? index_->find_room(vnum) : nullptr) {
      where = QStringLiteral("#%1 %2 (area %3)")
                  .arg(vnum)
                  .arg(QString::fromStdString(ent->label), QString::fromStdString(ent->area_name));
      known = true;
    } else {
      where = unknown ? QStringLiteral("#%1 (stanza fuori dal workspace)").arg(vnum)
                      : QStringLiteral("<span style='color:#e08a2e'>#%1 non trovata</span>").arg(vnum);
    }
  }
  dest_label_->setText(where);
  const bool self = ok && vnum == room_vnum_;
  two_way_->setEnabled(known && !self);
  {
    const QSignalBlocker b(two_way_);
    if(!known || self) {
      two_way_->setChecked(false);
    } else if(!two_way_touched_) {
      two_way_->setChecked(two_way_default_);
    }
  }
}

void ExitDialog::updateKeyLabel() {
  bool ok = false;
  const std::int64_t v = key_vnum_->text().trimmed().toLongLong(&ok);
  if(!ok || v <= 0) {
    key_label_->clear();
    return;
  }
  if(area_ != nullptr && area_->objects.count(v)) {
    key_label_->setText(QString::fromStdString(area_->objects.at(v).short_description));
  } else if(const auto* ent = index_ ? index_->find_obj(v) : nullptr) {
    key_label_->setText(QStringLiteral("%1 (area %2)")
                            .arg(QString::fromStdString(ent->label), QString::fromStdString(ent->area_name)));
  } else {
    key_label_->setText(QStringLiteral("oggetto non trovato nel workspace"));
  }
}

void ExitDialog::acceptExit() {
  bool ok = false;
  const std::int64_t vnum = selectedVnum(&ok);
  if(!ok || vnum <= 0) {
    QMessageBox::warning(this, windowTitle(), QStringLiteral("Scegli la stanza di destinazione."));
    return;
  }
  const int type = door_type_->currentData().toInt();
  std::int64_t key = -1;
  if(type == 2) {
    key = key_vnum_->text().trimmed().toLongLong(&ok);
    if(!ok || key <= 0) {
      QMessageBox::warning(this, windowTitle(), QStringLiteral("Scrivi il vnum della chiave."));
      return;
    }
  } else if(original_.present && original_.key <= 0) {
    key = original_.key;  // conserva la forma originale (0 o -1)
  }

  eleutheria::Exit ex = original_;
  ex.present = true;
  ex.to_room = vnum;
  ex.key = key;
  ex.keyword = keywords_->text().trimmed().toStdString();
  ex.general_description = description_->toPlainText().toStdString();
  // Bit gestiti dalla finestra; quelli sconosciuti (es. 512) restano com'erano.
  std::int64_t managed = kIsDoor;
  for(auto* cb : flag_boxes_) {
    managed |= std::int64_t{1} << cb->property("bit").toInt();
  }
  std::int64_t info = original_.exit_info & ~managed;
  if(type > 0) {
    info |= kIsDoor;
    for(auto* cb : flag_boxes_) {
      if(cb->isChecked()) {
        info |= std::int64_t{1} << cb->property("bit").toInt();
      }
    }
  } else {
    info &= ~(kClosed | kLocked);  // senza porta non puo' essere chiusa
  }
  if(info != original_.exit_info) {
    ex.exit_info_token.clear();  // il formato originale non vale piu'
  }
  ex.exit_info = info;

  result_ = {};
  result_.exit = ex;
  result_.state = type > 0 ? static_cast<SpawnState>(spawn_state_->currentData().toInt())
                           : SpawnState::NoReset;
  result_.two_way = two_way_->isEnabled() && two_way_->isChecked();
  if(area_ != nullptr && area_->rooms.count(vnum)) {
    result_.dest_is_current_area = true;
    result_.dest_area = area_folder_;
  } else if(const auto* ent = index_ ? index_->find_room(vnum) : nullptr) {
    result_.dest_area = QString::fromStdString(ent->area_name);
  }
  accept();
}

}  // namespace exit_ui
