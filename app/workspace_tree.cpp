#include "workspace_tree.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>

namespace ws_tree {

bool Item::operator<(const QTreeWidgetItem& other) const {
  // Gruppi (progetti / senza progetto): ordine fisso, indipendente dalla colonna.
  if(parent() == nullptr && other.parent() == nullptr) {
    // Con ordinamento decrescente Qt inverte il risultato: compensiamo, cosi' i
    // progetti restano sempre in ordine e "Aree senza progetto" sempre in fondo.
    const bool less = data(0, OrderRole).toString() < other.data(0, OrderRole).toString();
    const bool desc = treeWidget() != nullptr &&
                      treeWidget()->header()->sortIndicatorOrder() == Qt::DescendingOrder;
    return desc ? !less : less;
  }
  const int col = treeWidget() ? treeWidget()->sortColumn() : 0;
  const QVariant a = data(col, SortRole);
  const QVariant b = other.data(col, SortRole);
  if(a.typeId() == QMetaType::LongLong || b.typeId() == QMetaType::LongLong) {
    return a.toLongLong() < b.toLongLong();
  }
  return a.toString().compare(b.toString(), Qt::CaseInsensitive) < 0;
}

Tree::Tree(QWidget* parent) : QTreeWidget(parent) {
  setSelectionMode(QAbstractItemView::ExtendedSelection);
  setDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(true);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDefaultDropAction(Qt::CopyAction);
  setExpandsOnDoubleClick(false);
  setRootIsDecorated(true);
  setUniformRowHeights(true);
  setAllColumnsShowFocus(true);
}

QStringList Tree::selectedAreaFolders() const {
  QStringList out;
  for(auto* item : selectedItems()) {
    if(item->data(0, KindRole).toInt() == KindArea) {
      out << item->data(0, FolderRole).toString();
    }
  }
  return out;
}

bool Tree::targetProject(const QPoint& pos, QString* project) const {
  QTreeWidgetItem* item = itemAt(pos);
  if(item == nullptr) {
    return false;
  }
  if(item->parent() != nullptr) {
    item = item->parent();
  }
  const int kind = item->data(0, KindRole).toInt();
  if(kind != KindProject && kind != KindUnassigned) {
    return false;
  }
  *project = item->data(0, ProjectRole).toString();
  return true;
}

void Tree::dragEnterEvent(QDragEnterEvent* event) {
  if(event->source() == this && !selectedAreaFolders().isEmpty()) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
  } else {
    event->ignore();
  }
}

void Tree::dragMoveEvent(QDragMoveEvent* event) {
  QString project;
  if(event->source() == this && targetProject(event->position().toPoint(), &project)) {
    event->setDropAction(Qt::CopyAction);
    event->accept();
    if(QTreeWidgetItem* item = itemAt(event->position().toPoint())) {
      setCurrentItem(item->parent() ? item->parent() : item, 0, QItemSelectionModel::NoUpdate);
    }
  } else {
    event->ignore();
  }
}

void Tree::dropEvent(QDropEvent* event) {
  QString project;
  const QStringList folders = selectedAreaFolders();
  if(event->source() != this || folders.isEmpty() ||
     !targetProject(event->position().toPoint(), &project)) {
    event->ignore();
    return;
  }
  // Non chiamare QTreeWidget::dropEvent: non vogliamo che sposti gli item da solo,
  // l'elenco viene ricostruito dai dati dei progetti.
  event->setDropAction(Qt::CopyAction);
  event->accept();
  emit areasDropped(folders, project);
}

}  // namespace ws_tree
