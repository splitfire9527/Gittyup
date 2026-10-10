#ifndef REPOSITORYGROUPS_H
#define REPOSITORYGROUPS_H

#include <QObject>
#include <QStringList>

class RepositoryGroup;

// A repository path that belongs to a group. QObject so sidebar models can
// keep it as an internal pointer.
class GroupRepository : public QObject {
  Q_OBJECT

public:
  GroupRepository(const QString &path, RepositoryGroup *group);

  QString path() const { return mPath; }
  RepositoryGroup *group() const;

private:
  QString mPath;
};

class RepositoryGroup : public QObject {
  Q_OBJECT

public:
  RepositoryGroup(const QString &name, QObject *parent = nullptr);

  QString name() const { return mName; }
  void setName(const QString &name);

  int count() const { return mRepos.size(); }
  GroupRepository *repository(int index) const;
  QStringList paths() const;
  bool contains(const QString &path) const;
  void addPath(const QString &path);
  void removePath(const QString &path);

signals:
  void changed();

private:
  QString mName;
  QList<GroupRepository *> mRepos;
};

class RepositoryGroups : public QObject {
  Q_OBJECT

public:
  static RepositoryGroups *instance();

  int count() const { return mGroups.size(); }
  RepositoryGroup *at(int index) const;
  int indexOf(const RepositoryGroup *group) const;

  RepositoryGroup *add(const QString &name);
  void remove(RepositoryGroup *group);
  RepositoryGroup *groupForPath(const QString &path) const;
  bool containsPath(const QString &path) const;
  void addPath(RepositoryGroup *group, const QString &path);

signals:
  void groupsChanged();

private:
  RepositoryGroups(QObject *parent = nullptr);
  void load();
  void store();

  QList<RepositoryGroup *> mGroups;
};

#endif
