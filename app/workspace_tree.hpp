#pragma once

#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

// Elenco del pannello Workspace raggruppato per progetto:
//   riga progetto (stile diverso) → sotto le sue aree
//   ...
//   "Aree senza progetto" → sotto le aree non assegnate
// Le aree si possono trascinare su una riga progetto (o su un'area del progetto).
namespace ws_tree {

enum Role {
  KindRole = Qt::UserRole + 40,   // int Kind
  PathRole,                       // area: percorso cartella
  FolderRole,                     // area: nome cartella
  ProjectRole,                    // progetto (vuoto = senza progetto)
  OrderRole,                      // ordine dei gruppi
  SortRole,                       // valore per ordinare la colonna
};

enum Kind { KindProject = 1, KindUnassigned = 2, KindArea = 3 };

class Item : public QTreeWidgetItem {
 public:
  using QTreeWidgetItem::QTreeWidgetItem;
  bool operator<(const QTreeWidgetItem& other) const override;
};

class Tree : public QTreeWidget {
  Q_OBJECT
 public:
  explicit Tree(QWidget* parent = nullptr);

  QStringList selectedAreaFolders() const;

 signals:
  // Aree trascinate su un progetto (project vuoto = "Aree senza progetto").
  void areasDropped(const QStringList& folders, const QString& project);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  bool targetProject(const QPoint& pos, QString* project) const;
};

}  // namespace ws_tree
