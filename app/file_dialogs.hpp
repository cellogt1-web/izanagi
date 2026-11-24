#pragma once

#include <QString>
#include <QWidget>

namespace eleutheria_ui {

// Dialoghi file/cartella affidabili (su macOS il dialogo nativo spesso non compare
// senza Info.plist/attivazione corretta). Usano la maschera Qt di navigazione disco.
QString chooseExistingDirectory(QWidget* parent, const QString& title,
                                const QString& start_dir = QString());

QString chooseOpenFile(QWidget* parent, const QString& title, const QString& filter,
                       const QString& start_dir = QString());

// Un solo dialogo per file o cartelle: doppio clic/Apri su un file, oppure il
// pulsante "Apri cartella" per la cartella selezionata o quella corrente.
QString chooseFileOrDirectory(QWidget* parent, const QString& title, const QString& filter,
                              const QString& start_dir = QString());

QString chooseSaveFile(QWidget* parent, const QString& title, const QString& filter,
                       const QString& start_dir = QString(),
                       const QString& suggested_name = QString());

}  // namespace eleutheria_ui
