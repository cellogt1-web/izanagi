#pragma once

#include "eleutheria/catalog.hpp"
#include "eleutheria/types.hpp"
#include "eleutheria/workspace_index.hpp"

#include <QDialog>
#include <QString>
#include <cstdint>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

namespace exit_ui {

QString dir_name(int dir);       // "nord", "est", ...
int opposite_dir(int dir);       // N<->S, E<->W, U<->D

/** Stato della porta allo spawn (reset D della zona). */
enum class SpawnState { NoReset = -1, Open = 0, Closed = 1, Locked = 2 };

/** Stato attuale letto dai reset D della zona per (stanza, direzione). */
SpawnState find_spawn_state(const eleutheria::Area& area, std::int64_t room, int dir);

/**
 * Scrive l'uscita nella stanza dell'area e aggiorna il reset D (aggiunge, modifica o toglie).
 * Marca stanza e zona come modificate. Ritorna false se la stanza non esiste nell'area.
 */
bool apply_exit(eleutheria::Area& area, std::int64_t room, int dir, const eleutheria::Exit& exit,
                SpawnState state);

/** Esito della finestra. */
struct ExitDialogResult {
  bool remove = false;            // "Elimina uscita"
  eleutheria::Exit exit;
  SpawnState state = SpawnState::NoReset;
  bool two_way = false;
  QString dest_area;              // cartella area della destinazione ("" = sconosciuta)
  bool dest_is_current_area = false;
};

class ExitDialog : public QDialog {
  Q_OBJECT
 public:
  ExitDialog(QWidget* parent, const eleutheria::Catalog& catalog, const eleutheria::Area* area,
             const QString& area_folder, const eleutheria::WorkspaceEntityIndex* index,
             std::int64_t room_vnum, const QString& room_name, int dir,
             const eleutheria::Exit& current, SpawnState current_state);

  ExitDialogResult result() const { return result_; }

 private:
  void fillRooms();
  void updateEnabled();
  void updateKeyLabel();
  void acceptExit();
  std::int64_t selectedVnum(bool* ok) const;

  const eleutheria::Catalog& catalog_;
  const eleutheria::Area* area_;
  QString area_folder_;
  const eleutheria::WorkspaceEntityIndex* index_;
  std::int64_t room_vnum_;
  int dir_;
  eleutheria::Exit original_;
  ExitDialogResult result_;
  bool two_way_default_ = false;
  bool two_way_touched_ = false;

  QComboBox* dest_area_ = nullptr;
  QLineEdit* room_filter_ = nullptr;
  QListWidget* room_list_ = nullptr;
  QLineEdit* manual_vnum_ = nullptr;
  QLabel* dest_label_ = nullptr;
  QCheckBox* two_way_ = nullptr;
  QComboBox* door_type_ = nullptr;
  QLineEdit* key_vnum_ = nullptr;
  QLabel* key_label_ = nullptr;
  QComboBox* spawn_state_ = nullptr;
  QList<QCheckBox*> flag_boxes_;
  QLineEdit* keywords_ = nullptr;
  QPlainTextEdit* description_ = nullptr;
};

}  // namespace exit_ui
