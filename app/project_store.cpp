#include "project_store.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

ProjectStore::ProjectStore(QObject* parent) : QObject(parent) {}

bool ProjectStore::load(const QString& workspace_root, QString* error) {
  root_ = workspace_root;
  projects_.clear();
  QFile file(QDir(root_).filePath(fileName()));
  if(!file.exists()) {
    emit changed();
    return true;
  }
  if(!file.open(QIODevice::ReadOnly)) {
    if(error) *error = file.errorString();
    emit changed();
    return false;
  }
  QJsonParseError perr;
  const auto doc = QJsonDocument::fromJson(file.readAll(), &perr);
  if(perr.error != QJsonParseError::NoError || !doc.isObject()) {
    if(error) *error = perr.errorString();
    emit changed();
    return false;
  }
  for(const auto& v : doc.object().value(QStringLiteral("progetti")).toArray()) {
    const auto o = v.toObject();
    Project p;
    p.name = o.value(QStringLiteral("nome")).toString().trimmed();
    if(p.name.isEmpty()) {
      continue;
    }
    p.description = o.value(QStringLiteral("descrizione")).toString();
    p.notes = o.value(QStringLiteral("note")).toString();
    for(const auto& a : o.value(QStringLiteral("aree")).toArray()) {
      p.areas << a.toString();
    }
    for(const auto& t : o.value(QStringLiteral("todo")).toArray()) {
      const auto to = t.toObject();
      p.todos.append({to.value(QStringLiteral("testo")).toString(),
                      to.value(QStringLiteral("fatto")).toBool()});
    }
    projects_.append(p);
  }
  emit changed();
  return true;
}

bool ProjectStore::save(QString* error) const {
  if(root_.isEmpty()) {
    return false;
  }
  QJsonArray arr;
  for(const auto& p : projects_) {
    QJsonObject o;
    o.insert(QStringLiteral("nome"), p.name);
    o.insert(QStringLiteral("descrizione"), p.description);
    o.insert(QStringLiteral("note"), p.notes);
    o.insert(QStringLiteral("aree"), QJsonArray::fromStringList(p.areas));
    QJsonArray todos;
    for(const auto& t : p.todos) {
      QJsonObject to;
      to.insert(QStringLiteral("testo"), t.text);
      to.insert(QStringLiteral("fatto"), t.done);
      todos.append(to);
    }
    o.insert(QStringLiteral("todo"), todos);
    arr.append(o);
  }
  QJsonObject root;
  root.insert(QStringLiteral("formato"), 1);
  root.insert(QStringLiteral("progetti"), arr);

  QSaveFile file(QDir(root_).filePath(fileName()));
  if(!file.open(QIODevice::WriteOnly)) {
    if(error) *error = file.errorString();
    return false;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  if(!file.commit()) {
    if(error) *error = file.errorString();
    return false;
  }
  return true;
}

void ProjectStore::saveQuietly() const {
  save();
}

QStringList ProjectStore::projectNames() const {
  QStringList names;
  for(const auto& p : projects_) {
    names << p.name;
  }
  names.sort(Qt::CaseInsensitive);
  return names;
}

Project* ProjectStore::find(const QString& name) {
  for(auto& p : projects_) {
    if(p.name.compare(name, Qt::CaseInsensitive) == 0) {
      return &p;
    }
  }
  return nullptr;
}

const Project* ProjectStore::find(const QString& name) const {
  for(const auto& p : projects_) {
    if(p.name.compare(name, Qt::CaseInsensitive) == 0) {
      return &p;
    }
  }
  return nullptr;
}

QString ProjectStore::projectOf(const QString& area_folder) const {
  for(const auto& p : projects_) {
    if(p.areas.contains(area_folder, Qt::CaseInsensitive)) {
      return p.name;
    }
  }
  return {};
}

bool ProjectStore::createProject(const QString& name) {
  const QString n = name.trimmed();
  if(n.isEmpty() || find(n) != nullptr) {
    return false;
  }
  Project p;
  p.name = n;
  projects_.append(p);
  saveQuietly();
  emit changed();
  return true;
}

bool ProjectStore::renameProject(const QString& old_name, const QString& new_name) {
  const QString n = new_name.trimmed();
  Project* p = find(old_name);
  if(p == nullptr || n.isEmpty()) {
    return false;
  }
  const Project* other = find(n);
  if(other != nullptr && other != p) {
    return false;
  }
  p->name = n;
  saveQuietly();
  emit changed();
  return true;
}

void ProjectStore::deleteProject(const QString& name) {
  projects_.erase(std::remove_if(projects_.begin(), projects_.end(),
                                 [&](const Project& p) {
                                   return p.name.compare(name, Qt::CaseInsensitive) == 0;
                                 }),
                  projects_.end());
  saveQuietly();
  emit changed();
}

void ProjectStore::assignAreas(const QStringList& area_folders, const QString& name) {
  for(auto& p : projects_) {
    for(const auto& a : area_folders) {
      p.areas.removeAll(a);
    }
  }
  if(Project* target = name.isEmpty() ? nullptr : find(name)) {
    for(const auto& a : area_folders) {
      if(!target->areas.contains(a)) {
        target->areas.append(a);
      }
    }
  }
  saveQuietly();
  emit changed();
}

void ProjectStore::updateProject(const Project& project) {
  if(Project* p = find(project.name)) {
    p->description = project.description;
    p->notes = project.notes;
    p->todos = project.todos;
    saveQuietly();
  }
}
