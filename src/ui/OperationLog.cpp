#include "OperationLog.h"
#include "git/Branch.h"
#include "git/Reference.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

OperationLog *OperationLog::instance() {
  static OperationLog *log = new OperationLog;
  return log;
}

bool OperationLog::matches(const git::Repository &repo, const Entry &entry) {
  return repo.isValid() &&
         QDir(repo.workdir().path()) == QDir(entry.repoPath);
}

OperationLog::Entry
OperationLog::takeSnapshot(const git::Repository &repo,
                           const QString &description) const {
  Entry entry;
  entry.description = description;
  if (!repo.isValid())
    return entry;

  entry.repoPath = repo.workdir().path();
  entry.detached = repo.isHeadDetached();
  git::Reference head = repo.head();
  if (head.isValid()) {
    entry.headName = head.qualifiedName();
    if (head.target().isValid())
      entry.headId = head.target().id().toString();
  }

  for (const git::Reference &ref : repo.refs()) {
    if (!ref.isValid() || !ref.isLocalBranch() || !ref.target().isValid())
      continue;
    entry.refs.insert(ref.qualifiedName(), ref.target().id().toString());
  }
  return entry;
}

void OperationLog::capture(const git::Repository &repo,
                           const QString &description) {
  if (!repo.isValid())
    return;
  mUndo.append(takeSnapshot(repo, description));
  while (mUndo.size() > 50)
    mUndo.removeFirst();
  mRedo.clear();
  emit changed();
}

void OperationLog::captureFiles(const git::Repository &repo,
                                const QStringList &paths,
                                const QString &description) {
  if (!repo.isValid() || paths.isEmpty())
    return;
  Entry entry = takeSnapshot(repo, description);
  entry.hasFiles = true;
  QDir workdir = repo.workdir();
  for (const QString &path : paths) {
    QFile file(workdir.filePath(path));
    if (file.open(QFile::ReadOnly))
      entry.files.insert(path, file.readAll());
    else
      entry.files.insert(path, QByteArray());
  }
  mUndo.append(entry);
  while (mUndo.size() > 50)
    mUndo.removeFirst();
  mRedo.clear();
  emit changed();
}

bool OperationLog::canUndo(const git::Repository &repo) const {
  return !mUndo.isEmpty() && matches(repo, mUndo.last());
}

bool OperationLog::canRedo(const git::Repository &repo) const {
  return !mRedo.isEmpty() && matches(repo, mRedo.last());
}

QString OperationLog::undoDescription(const git::Repository &repo) const {
  return canUndo(repo) ? mUndo.last().description : QString();
}

QString OperationLog::redoDescription(const git::Repository &repo) const {
  return canRedo(repo) ? mRedo.last().description : QString();
}

bool OperationLog::restore(git::Repository repo, const Entry &entry) {
  if (!matches(repo, entry))
    return false;

  for (auto it = entry.refs.cbegin(); it != entry.refs.cend(); ++it) {
    git::Commit commit = repo.lookupCommit(it.value());
    if (!commit.isValid())
      continue;
    git::Reference ref = repo.lookupRef(it.key());
    if (ref.isValid()) {
      if (!ref.target().isValid() || ref.target().id().toString() != it.value())
        ref.setTarget(commit, "gittyup undo");
    } else if (it.key().startsWith("refs/heads/")) {
      repo.createBranch(it.key().mid(11), commit, true);
    }
  }

  if (!entry.headId.isEmpty()) {
    git::Commit commit = repo.lookupCommit(entry.headId);
    if (commit.isValid()) {
      if (entry.detached || entry.headName.isEmpty())
        repo.setHeadDetached(commit);
      else if (git::Reference ref = repo.lookupRef(entry.headName))
        repo.setHead(ref);
    }
  }

  if (entry.hasFiles) {
    QDir workdir = repo.workdir();
    for (auto it = entry.files.cbegin(); it != entry.files.cend(); ++it) {
      QSaveFile file(workdir.filePath(it.key()));
      QDir().mkpath(QFileInfo(file.fileName()).path());
      if (!file.open(QFile::WriteOnly))
        continue;
      file.write(it.value());
      file.commit();
    }
  }
  return true;
}

bool OperationLog::undo(git::Repository repo) {
  if (!canUndo(repo))
    return false;
  Entry current = takeSnapshot(repo, mUndo.last().description);
  current.hasFiles = mUndo.last().hasFiles;
  if (current.hasFiles) {
    QDir workdir = repo.workdir();
    for (auto it = mUndo.last().files.cbegin(); it != mUndo.last().files.cend();
         ++it) {
      QFile file(workdir.filePath(it.key()));
      current.files.insert(it.key(),
                           file.open(QFile::ReadOnly) ? file.readAll()
                                                      : QByteArray());
    }
  }
  Entry entry = mUndo.takeLast();
  bool ok = restore(repo, entry);
  mRedo.append(current);
  emit changed();
  return ok;
}

bool OperationLog::redo(git::Repository repo) {
  if (!canRedo(repo))
    return false;
  Entry current = takeSnapshot(repo, mRedo.last().description);
  Entry entry = mRedo.takeLast();
  bool ok = restore(repo, entry);
  mUndo.append(current);
  emit changed();
  return ok;
}
