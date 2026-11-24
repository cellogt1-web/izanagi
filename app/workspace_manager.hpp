#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace eleutheria_ui {

// Elenco dei workspace conosciuti (salvato in QSettings).
QStringList knownWorkspaces();
void addKnownWorkspace(const QString& path);
void removeKnownWorkspace(const QString& path);
QString normalizeWorkspacePath(const QString& path);

// Cartella dell'area di esempio (B1) distribuita con il programma, vuota se assente.
QString findSampleArea();

// Testo che spiega com'e' fatto un workspace (usato nei messaggi d'errore).
QString workspaceRuleText();

class WorkspaceManagerDialog : public QDialog {
  Q_OBJECT
 public:
  enum class Choice { None, OpenWorkspace, OpenSample };

  WorkspaceManagerDialog(QWidget* parent, const QString& current_workspace,
                         const QString& warning = QString());

  Choice choice() const { return choice_; }
  QString chosenPath() const { return chosen_path_; }

 private:
  void refresh(const QString& select_path = QString());
  QString selectedPath() const;
  void updateButtons();
  void openSelected();
  void createNew();
  void addExisting();
  void removeSelected();

  QString current_;
  Choice choice_ = Choice::None;
  QString chosen_path_;
  QTreeWidget* list_ = nullptr;
  QLabel* intro_ = nullptr;
  QPushButton* open_btn_ = nullptr;
  QPushButton* remove_btn_ = nullptr;
};

}  // namespace eleutheria_ui
