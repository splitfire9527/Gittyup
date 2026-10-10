#include "RepositoryGroups.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace {

QString clean(const QString &path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

GroupRepository::GroupRepository(const QString &path, RepositoryGroup *group)
    : QObject(group), mPath(clean(path)) {}

RepositoryGroup *GroupRepository::group() const {
  return static_cast<RepositoryGroup *>(parent());
}

RepositoryGroup::RepositoryGroup(const QString &name, QObject *parent)
    : QObject(parent), mName(name) {}

void RepositoryGroup::setName(const QString &name) {
  if (mName == name)
    return;
  mName = name;
  emit changed();
}

GroupRepository *RepositoryGroup::repository(int index) const {
  return mRepos.at(index);
}

QStringList RepositoryGroup::paths() const {
  QStringList paths;
  for (GroupRepository *repo : mRepos)
    paths.append(repo->path());
  return paths;
}

bool RepositoryGroup::contains(const QString &path) const {
  const QString cleaned = clean(path);
  for (GroupRepository *repo : mRepos) {
    if (repo->path() == cleaned)
      return true;
  }
  return false;
}

void RepositoryGroup::addPath(const QString &path) {
  const QString cleaned = clean(path);
  if (cleaned.isEmpty() || contains(cleaned))
    return;
  mRepos.append(new GroupRepository(cleaned, this));
  emit changed();
}

void RepositoryGroup::removePath(const QString &path) {
  const QString cleaned = clean(path);
  for (int i = 0; i < mRepos.size(); ++i) {
    if (mRepos.at(i)->path() == cleaned) {
      delete mRepos.takeAt(i);
      emit changed();
      return;
    }
  }
}

RepositoryGroups::RepositoryGroups(QObject *parent) : QObject(parent) { load(); }

RepositoryGroups *RepositoryGroups::instance() {
  static RepositoryGroups *groups = new RepositoryGroups;
  return groups;
}

RepositoryGroup *RepositoryGroups::at(int index) const {
  return mGroups.at(index);
}

int RepositoryGroups::indexOf(const RepositoryGroup *group) const {
  return mGroups.indexOf(const_cast<RepositoryGroup *>(group));
}

RepositoryGroup *RepositoryGroups::add(const QString &name) {
  auto *group = new RepositoryGroup(name, this);
  mGroups.append(group);
  connect(group, &RepositoryGroup::changed, this, [this] {
    store();
    emit groupsChanged();
  });
  store();
  emit groupsChanged();
  return group;
}

void RepositoryGroups::remove(RepositoryGroup *group) {
  if (!mGroups.removeOne(group))
    return;
  group->deleteLater();
  store();
  emit groupsChanged();
}

RepositoryGroup *RepositoryGroups::groupForPath(const QString &path) const {
  for (RepositoryGroup *group : mGroups) {
    if (group->contains(path))
      return group;
  }
  return nullptr;
}

bool RepositoryGroups::containsPath(const QString &path) const {
  return groupForPath(path) != nullptr;
}

void RepositoryGroups::addPath(RepositoryGroup *group, const QString &path) {
  if (!group)
    return;
  for (RepositoryGroup *other : mGroups) {
    if (other != group)
      other->removePath(path);
  }
  group->addPath(path);
}

void RepositoryGroups::load() {
  QSettings settings;
  int size = settings.beginReadArray("repositoryGroups");
  for (int i = 0; i < size; ++i) {
    settings.setArrayIndex(i);
    auto *group = new RepositoryGroup(settings.value("name").toString(), this);
    for (const QString &path : settings.value("paths").toStringList())
      group->addPath(path);
    mGroups.append(group);
    connect(group, &RepositoryGroup::changed, this, [this] {
      store();
      emit groupsChanged();
    });
  }
  settings.endArray();
}

void RepositoryGroups::store() {
  QSettings settings;
  settings.beginWriteArray("repositoryGroups");
  for (int i = 0; i < mGroups.size(); ++i) {
    settings.setArrayIndex(i);
    settings.setValue("name", mGroups.at(i)->name());
    settings.setValue("paths", mGroups.at(i)->paths());
  }
  settings.endArray();
}
