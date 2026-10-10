#ifndef INTERACTIVEREBASEDIALOG_H
#define INTERACTIVEREBASEDIALOG_H

#include "git/Commit.h"
#include "git/Repository.h"
#include <QDialog>

class QTableWidget;

// Visual interactive rebase of the commits after onto, up to HEAD.
class InteractiveRebaseDialog : public QDialog {
  Q_OBJECT

public:
  static void start(const git::Repository &repo, const git::Commit &onto,
                    QWidget *parent = nullptr);

private:
  InteractiveRebaseDialog(const git::Repository &repo, const git::Commit &onto,
                          const QList<git::Commit> &commits, QWidget *parent);

  void moveCurrent(int delta);
  void setActionForSelection(const QString &action);
  void apply();

  git::Repository mRepo;
  git::Commit mOnto;
  QTableWidget *mTable;
};

#endif
