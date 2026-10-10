#ifndef OPERATIONLOG_H
#define OPERATIONLOG_H

#include "git/Repository.h"
#include <QHash>
#include <QObject>

// Remembers Gittyup-initiated repository mutations so they can be undone.
class OperationLog : public QObject {
  Q_OBJECT

public:
  static OperationLog *instance();

  void capture(const git::Repository &repo, const QString &description);
  void captureFiles(const git::Repository &repo, const QStringList &paths,
                    const QString &description);

  bool canUndo(const git::Repository &repo) const;
  bool canRedo(const git::Repository &repo) const;
  QString undoDescription(const git::Repository &repo) const;
  QString redoDescription(const git::Repository &repo) const;

  bool undo(git::Repository repo);
  bool redo(git::Repository repo);

signals:
  void changed();

private:
  struct Entry {
    QString repoPath;
    QString description;
    bool detached = false;
    QString headName;
    QString headId;
    QHash<QString, QString> refs;
    bool hasFiles = false;
    QHash<QString, QByteArray> files;
  };

  OperationLog() = default;
  Entry takeSnapshot(const git::Repository &repo, const QString &description) const;
  bool restore(git::Repository repo, const Entry &entry);
  static bool matches(const git::Repository &repo, const Entry &entry);

  QList<Entry> mUndo;
  QList<Entry> mRedo;
};

#endif
