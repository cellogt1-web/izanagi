#pragma once

#include "eleutheria/workspace.hpp"

#include <QString>
#include <QWidget>
#include <vector>

class ProjectStore;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QTimer;

// Pannello di un progetto: aree contenute con i loro dati, totali, vnum occupati,
// descrizione, note e lista To-do. Le modifiche si salvano da sole nel file progetti.
class ProjectPanel : public QWidget {
  Q_OBJECT
 public:
  ProjectPanel(ProjectStore* store, const QString& project_name, QWidget* parent = nullptr);

  QString projectName() const { return name_; }
  void setProjectName(const QString& name);
  // Ricalcola tabella e totali (chiamare quando il workspace o il progetto cambiano).
  void refresh(const std::vector<eleutheria::WorkspaceArea>& areas);
  void flush();  // salva subito le modifiche in sospeso

 signals:
  void openAreaRequested(const QString& area_path);
  void closeRequested();

 private:
  void loadTexts();
  void scheduleSave();
  void saveNow();
  void addTodo();
  void removeSelectedTodos();

  ProjectStore* store_;
  QString name_;
  bool loading_ = false;

  QLabel* summary_ = nullptr;
  QTableWidget* areas_table_ = nullptr;
  QPlainTextEdit* description_ = nullptr;
  QPlainTextEdit* notes_ = nullptr;
  QListWidget* todo_list_ = nullptr;
  QLineEdit* todo_input_ = nullptr;
  QTimer* save_timer_ = nullptr;
  std::vector<eleutheria::WorkspaceArea> areas_cache_;
};
