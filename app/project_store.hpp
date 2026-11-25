#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

// Progetti del workspace: raggruppano le aree SOLO per visualizzazione/organizzazione.
// Salvati in <workspace>/izanagi-progetti.json; i file delle aree non vengono toccati.

struct ProjectTodo {
  QString text;
  bool done = false;
};

struct Project {
  QString name;
  QString description;
  QString notes;
  QStringList areas;  // nomi cartella delle aree
  QList<ProjectTodo> todos;
};

class ProjectStore : public QObject {
  Q_OBJECT
 public:
  explicit ProjectStore(QObject* parent = nullptr);

  static QString fileName() { return QStringLiteral("izanagi-progetti.json"); }

  // Carica (o svuota se il file non c'e'). Ritorna false con messaggio se il file e' illeggibile.
  bool load(const QString& workspace_root, QString* error = nullptr);
  bool save(QString* error = nullptr) const;
  bool isLoaded() const { return !root_.isEmpty(); }

  const QList<Project>& projects() const { return projects_; }
  QStringList projectNames() const;
  Project* find(const QString& name);
  const Project* find(const QString& name) const;
  QString projectOf(const QString& area_folder) const;  // "" = nessun progetto

  bool createProject(const QString& name);
  bool renameProject(const QString& old_name, const QString& new_name);
  void deleteProject(const QString& name);
  // Sposta le aree nel progetto (le toglie da quello in cui erano). name vuoto = nessun progetto.
  void assignAreas(const QStringList& area_folders, const QString& name);
  // Aggiorna descrizione/note/todo (salva e notifica).
  void updateProject(const Project& project);

 signals:
  void changed();  // struttura (progetti/aree) cambiata

 private:
  void saveQuietly() const;

  QString root_;
  QList<Project> projects_;
};
