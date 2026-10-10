#include "CommandPalette.h"
#include "FileHistoryWindow.h"
#include "MainWindow.h"
#include "OperationLog.h"
#include "DoubleTreeWidget.h"
#include "RepoView.h"
#include "conf/RecentRepositories.h"
#include "conf/RecentRepository.h"
#include "conf/RepositoryGroups.h"
#include "dialogs/InteractiveRebaseDialog.h"
#include "dialogs/SettingsDialog.h"
#include "dialogs/StartDialog.h"
#include "git/Reference.h"
#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <algorithm>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace {

int fuzzyScore(const QString &haystack, const QString &needle) {
  if (needle.isEmpty())
    return 1;
  int from = 0;
  int score = 0;
  int previous = -1;
  for (const QChar &ch : needle) {
    int at = haystack.indexOf(ch, from, Qt::CaseInsensitive);
    if (at < 0)
      return -1;
    score += 12 - qMin(11, previous < 0 ? at : at - previous);
    previous = at;
    from = at + 1;
  }
  return score;
}

} // namespace

void CommandPalette::open(QWidget *parent) {
  auto *palette = new CommandPalette(parent);
  palette->setAttribute(Qt::WA_DeleteOnClose);
  palette->QDialog::open();
}

CommandPalette::CommandPalette(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Quick Open"));
  resize(560, 420);

  mQuery = new QLineEdit(this);
  mQuery->setPlaceholderText(tr("Repository, branch, file, or command"));
  mList = new QListWidget(this);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(mQuery);
  layout->addWidget(mList);

  RepoView *view = nullptr;
  if (MainWindow *window = MainWindow::activeWindow())
    view = window->currentView();

  RecentRepositories *recent = RecentRepositories::instance();
  for (int i = 0; i < recent->count(); ++i) {
    RecentRepository *repo = recent->repository(i);
    QString path = repo->gitpath();
    mItems.append({repo->name(), path, [path] { MainWindow::open(path); }});
  }

  RepositoryGroups *groups = RepositoryGroups::instance();
  for (int i = 0; i < groups->count(); ++i) {
    RepositoryGroup *group = groups->at(i);
    for (const QString &path : group->paths()) {
      mItems.append({QFileInfo(path).fileName(),
                     tr("Group: %1").arg(group->name()),
                     [path] { MainWindow::open(path); }});
    }
  }

  if (view && view->repo().isValid()) {
    git::Repository repo = view->repo();
    for (const git::Reference &ref : repo.refs()) {
      if (!ref.isLocalBranch())
        continue;
      QString name = ref.name();
      mItems.append({name, tr("Branch"), [view, ref] {
                       view->checkout(ref);
                     }});
    }

    mItems.append({tr("Fetch"), tr("Command"), [view] { view->fetchAll(); }});
    mItems.append({tr("Pull"), tr("Command"), [view] { view->pull(); }});
    mItems.append({tr("Stash"), tr("Command"), [view] { view->promptToStash(); }});
    mItems.append({tr("Undo last Git operation"), tr("Command"), [view] {
                     if (OperationLog::instance()->canUndo(view->repo()))
                       OperationLog::instance()->undo(view->repo());
                     view->refresh();
                   }});
    mItems.append(
        {tr("Interactive rebase"), tr("Command"), [view] {
           if (git::Reference head = view->repo().head()) {
             if (git::Commit commit = head.target()) {
               if (!commit.parents().isEmpty())
                 InteractiveRebaseDialog::start(view->repo(),
                                                commit.parents().first(), view);
             }
           }
         }});
    mItems.append({tr("File history"), tr("Command"), [view] {
                     QString path;
                     if (DoubleTreeWidget *tree =
                             view->findChild<DoubleTreeWidget *>()) {
                       QModelIndex index = tree->selectedIndex();
                       if (index.isValid())
                         path = index.data(Qt::EditRole).toString();
                     }
                     if (!path.isEmpty())
                       FileHistoryWindow::show(view->repo(), path);
                   }});
  }

  mItems.append({tr("Repository chooser"), tr("Command"), [] {
                   StartDialog::openSharedInstance();
                 }});
  mItems.append({tr("Options"), tr("Command"), [] {
                   SettingsDialog::openSharedInstance();
                 }});

  connect(mQuery, &QLineEdit::textChanged, this, &CommandPalette::rebuild);
  connect(mList, &QListWidget::itemActivated, this,
          &CommandPalette::activateCurrent);
  connect(mQuery, &QLineEdit::returnPressed, this,
          &CommandPalette::activateCurrent);
  rebuild(QString());
  mQuery->setFocus();
}

void CommandPalette::rebuild(const QString &query) {
  QList<QPair<int, int>> ranked;
  for (int i = 0; i < mItems.size(); ++i) {
    int score = fuzzyScore(mItems.at(i).title + " " + mItems.at(i).detail, query);
    if (score >= 0)
      ranked.append({score, i});
  }
  std::sort(ranked.begin(), ranked.end(),
            [](const QPair<int, int> &a, const QPair<int, int> &b) {
              return a.first > b.first;
            });

  mList->clear();
  for (const auto &entry : ranked) {
    const Item &item = mItems.at(entry.second);
    auto *row = new QListWidgetItem(item.title);
    row->setData(Qt::UserRole, entry.second);
    row->setToolTip(item.detail);
    mList->addItem(row);
  }
  if (mList->count() > 0)
    mList->setCurrentRow(0);
}

void CommandPalette::activateCurrent() {
  QListWidgetItem *item = mList->currentItem();
  if (!item)
    return;
  int index = item->data(Qt::UserRole).toInt();
  if (index < 0 || index >= mItems.size())
    return;
  auto action = mItems.at(index).action;
  close();
  action();
}

void CommandPalette::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
    mList->setFocus();
    QApplication::sendEvent(mList, event);
    mQuery->setFocus();
    return;
  }
  QDialog::keyPressEvent(event);
}
