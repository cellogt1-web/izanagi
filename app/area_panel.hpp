#pragma once

#include "eleutheria/catalog.hpp"
#include "eleutheria/types.hpp"
#include "eleutheria/workspace_index.hpp"

#include <QString>
#include <QWidget>
#include <cstdint>
#include <optional>

class QListWidget;
class QTabWidget;
class QToolBar;
class RoomEditor;
class MobEditor;
class ObjEditor;
class ZoneEditor;
class SpeEditor;

// Pannello che contiene UNA area aperta: schede Stanze/Mob/Oggetti/Zone/Special,
// la sua barra comandi e tutta la logica di modifica/salvataggio di quell'area.
// Ogni area aperta ha il suo pannello: la finestra principale ne puo' avere piu' d'uno.
class AreaPanel : public QWidget {
  Q_OBJECT
 public:
  AreaPanel(const eleutheria::Catalog& catalog, eleutheria::Area area, const QString& base_path,
            QWidget* parent = nullptr);

  // Identita'
  QString basePath() const { return base_path_; }
  QString folderName() const;
  QString displayName() const;   // nome per titolo pannello (con * se modificata)
  bool isDirty() const;
  const eleutheria::Area& area() const { return area_; }

  // Impostazioni condivise dalla finestra principale
  void setWorkspaceIndex(eleutheria::WorkspaceEntityIndex* index, bool ready);
  void setMaxLineLength(int max_length);

  // Comandi
  bool save();     // false se annullato o errore
  bool saveAs();
  void validate();
  void addRoom();
  void addMob();
  void addObj();
  void deleteCurrent();
  void applyCurrentEditor();
  // Chiede se salvare le modifiche; false = l'utente ha annullato.
  bool maybeSave();

  QString countsText() const;

  /** Scrive un'uscita (es. di ritorno da un'altra area) in una stanza di questa area. */
  bool applyExternalExit(std::int64_t room, int dir, const eleutheria::Exit& exit, int state);

 signals:
  void titleChanged();
  void statusMessage(const QString& text, int timeout_ms);
  void closeRequested();
  void externalExitRequested(const QString& area_folder, qint64 room_vnum, int dir,
                             const eleutheria::Exit& exit, int state);

 private:
  void buildUi();
  void refreshLists();
  void markDirty();
  void applyWorkspaceIndexToEditors();
  void onRoomSelected();
  void onMobSelected();
  void onObjSelected();
  void onZoneSelected();
  void onSpeEdited();

  const eleutheria::Catalog& catalog_;
  eleutheria::Area area_;
  QString base_path_;

  eleutheria::WorkspaceEntityIndex* workspace_index_ = nullptr;
  bool workspace_index_ready_ = false;

  QToolBar* toolbar_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  QListWidget* room_list_ = nullptr;
  QListWidget* mob_list_ = nullptr;
  QListWidget* obj_list_ = nullptr;
  QListWidget* zone_list_ = nullptr;
  RoomEditor* room_editor_ = nullptr;
  MobEditor* mob_editor_ = nullptr;
  ObjEditor* obj_editor_ = nullptr;
  ZoneEditor* zone_editor_ = nullptr;
  SpeEditor* spe_editor_ = nullptr;

  std::optional<std::int64_t> current_room_;
  std::optional<std::int64_t> current_mob_;
  std::optional<std::int64_t> current_obj_;
  std::optional<int> current_zone_;
};
