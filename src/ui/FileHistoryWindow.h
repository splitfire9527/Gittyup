#ifndef FILEHISTORYWINDOW_H
#define FILEHISTORYWINDOW_H

#include "git/Commit.h"
#include "git/Repository.h"
#include "git/RevWalk.h"
#include <QMainWindow>

class QListWidget;
class QPlainTextEdit;
class QLabel;
class BlameEditor;

// Non-modal history of a file, directory, or line range.
class FileHistoryWindow : public QMainWindow {
  Q_OBJECT

public:
  static void show(const git::Repository &repo, const QString &path,
                   int lineStart = -1, int lineEnd = -1);

private:
  FileHistoryWindow(const git::Repository &repo, const QString &path,
                    int lineStart, int lineEnd, QWidget *parent = nullptr);

  struct Entry {
    git::Commit commit;
    QString path;
  };

  void scanMore();
  void showEntry(int row);
  void showDiff(const Entry &left, const Entry &right);
  QString renamedFrom(const git::Commit &commit, const QString &path) const;
  bool touchesRange(const git::Commit &commit, const QString &path, int *start,
                    int *end) const;

  git::Repository mRepo;
  QString mPath;
  int mLineStart = -1;
  int mLineEnd = -1;
  bool mDirectory = false;

  git::RevWalk mWalker;
  QString mScanPath;
  bool mScanning = true;

  QList<Entry> mEntries;
  QListWidget *mList;
  BlameEditor *mEditor;
  QPlainTextEdit *mDiff;
  QLabel *mStatus;
};

#endif
