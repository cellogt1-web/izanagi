#include "file_dialogs.hpp"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QPushButton>
#include <QStandardPaths>

namespace eleutheria_ui {
namespace {

void prepare_parent(QWidget* parent) {
  if(parent == nullptr) {
    return;
  }
  parent->raise();
  parent->activateWindow();
  QApplication::processEvents();
}

QString default_start(const QString& start_dir) {
  if(!start_dir.isEmpty()) {
    return start_dir;
  }
  const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  return home.isEmpty() ? QStringLiteral("/") : home;
}

void configure_browser(QFileDialog* dialog) {
  // Dialogo Qt (non nativo): sempre una maschera navigabile su Win/Mac/Linux.
  dialog->setOption(QFileDialog::DontUseNativeDialog, true);
  dialog->setViewMode(QFileDialog::Detail);
  dialog->setOption(QFileDialog::DontUseCustomDirectoryIcons, false);
  dialog->setModal(true);
}

}  // namespace

QString chooseExistingDirectory(QWidget* parent, const QString& title, const QString& start_dir) {
  prepare_parent(parent);
  QFileDialog dialog(parent, title, default_start(start_dir));
  configure_browser(&dialog);
  dialog.setFileMode(QFileDialog::Directory);
  dialog.setOption(QFileDialog::ShowDirsOnly, true);
  dialog.setAcceptMode(QFileDialog::AcceptOpen);
  if(dialog.exec() != QDialog::Accepted) {
    return {};
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString{} : files.front();
}

QString chooseOpenFile(QWidget* parent, const QString& title, const QString& filter,
                       const QString& start_dir) {
  prepare_parent(parent);
  QFileDialog dialog(parent, title, default_start(start_dir), filter);
  configure_browser(&dialog);
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setAcceptMode(QFileDialog::AcceptOpen);
  if(dialog.exec() != QDialog::Accepted) {
    return {};
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString{} : files.front();
}

QString chooseSaveFile(QWidget* parent, const QString& title, const QString& filter,
                       const QString& start_dir, const QString& suggested_name) {
  prepare_parent(parent);
  QString start = default_start(start_dir);
  if(!suggested_name.isEmpty()) {
    start += QLatin1Char('/') + suggested_name;
  }
  QFileDialog dialog(parent, title, start, filter);
  configure_browser(&dialog);
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setDefaultSuffix(QStringLiteral("ele"));
  if(dialog.exec() != QDialog::Accepted) {
    return {};
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString{} : files.front();
}

QString chooseFileOrDirectory(QWidget* parent, const QString& title, const QString& filter,
                              const QString& start_dir) {
  prepare_parent(parent);
  QFileDialog dialog(parent, title, default_start(start_dir), filter);
  configure_browser(&dialog);
  dialog.setFileMode(QFileDialog::ExistingFile);
  dialog.setAcceptMode(QFileDialog::AcceptOpen);

  // Il dialogo Qt sa scegliere o file o cartelle: aggiungiamo un pulsante che
  // apre la cartella selezionata (o quella in cui ci si trova).
  QString chosen_dir;
  QString highlighted_dir;
  auto* folder_button = new QPushButton(&dialog);
  auto update_button = [&]() {
    const QString target =
        highlighted_dir.isEmpty() ? dialog.directory().absolutePath() : highlighted_dir;
    folder_button->setText(QStringLiteral("Apri cartella \"%1\"")
                               .arg(QFileInfo(target).fileName().isEmpty()
                                        ? target
                                        : QFileInfo(target).fileName()));
    folder_button->setToolTip(target);
  };
  QObject::connect(&dialog, &QFileDialog::currentChanged, &dialog, [&](const QString& path) {
    highlighted_dir = QFileInfo(path).isDir() ? path : QString();
    update_button();
  });
  QObject::connect(&dialog, &QFileDialog::directoryEntered, &dialog, [&](const QString&) {
    highlighted_dir.clear();
    update_button();
  });
  QObject::connect(folder_button, &QPushButton::clicked, &dialog, [&]() {
    chosen_dir =
        highlighted_dir.isEmpty() ? dialog.directory().absolutePath() : highlighted_dir;
    static_cast<QDialog&>(dialog).done(QDialog::Accepted);
  });
  update_button();
  if(auto* grid = qobject_cast<QGridLayout*>(dialog.layout())) {
    grid->addWidget(folder_button, grid->rowCount(), 0, 1, grid->columnCount());
  }

  if(dialog.exec() != QDialog::Accepted) {
    return {};
  }
  if(!chosen_dir.isEmpty()) {
    return QDir::cleanPath(chosen_dir);
  }
  const auto files = dialog.selectedFiles();
  return files.isEmpty() ? QString{} : files.front();
}

}  // namespace eleutheria_ui
