#include "FileHistoryWindow.h"
#include "BlameEditor.h"
#include "OperationLog.h"
#include "git/Diff.h"
#include "git/Patch.h"
#include "git/Reference.h"
#include "git/RevWalk.h"
#include "git/Signature.h"
#include "git/Tree.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QDateTime>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const int kScanBatch = 25;
const int kScanLimit = 4000;

QString dayKey(const git::Commit &commit) {
  return commit.isValid() ? commit.author().date().date().toString(Qt::ISODate)
                          : QString();
}

} // namespace

void FileHistoryWindow::show(const git::Repository &repo, const QString &path,
                             int lineStart, int lineEnd) {
  if (!repo.isValid() || path.isEmpty())
    return;
  auto *window = new FileHistoryWindow(repo, path, lineStart, lineEnd);
  window->setAttribute(Qt::WA_DeleteOnClose);
  window->resize(1100, 700);
  window->QWidget::show();
}

FileHistoryWindow::FileHistoryWindow(const git::Repository &repo,
                                     const QString &path, int lineStart,
                                     int lineEnd, QWidget *parent)
    : QMainWindow(parent), mRepo(repo), mPath(path), mLineStart(lineStart),
      mLineEnd(lineEnd < 0 ? lineStart : lineEnd) {
  QString absolute = repo.workdir().absoluteFilePath(path);
  mDirectory = QFileInfo(absolute).isDir() || path.endsWith('/');
  setWindowTitle(tr("History — %1").arg(path));

  mList = new QListWidget(this);
  mList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  mList->setContextMenuPolicy(Qt::CustomContextMenu);

  mEditor = new BlameEditor(repo, this);
  mDiff = new QPlainTextEdit(this);
  mDiff->setReadOnly(true);
  mDiff->setLineWrapMode(QPlainTextEdit::NoWrap);

  auto *stack = new QStackedWidget(this);
  stack->addWidget(mEditor);
  stack->addWidget(mDiff);

  auto *content = new QPushButton(tr("File"), this);
  auto *diffButton = new QPushButton(tr("Diff"), this);
  content->setCheckable(true);
  diffButton->setCheckable(true);
  content->setChecked(true);
  connect(content, &QPushButton::clicked, this, [stack, content, diffButton] {
    stack->setCurrentIndex(0);
    content->setChecked(true);
    diffButton->setChecked(false);
  });
  connect(diffButton, &QPushButton::clicked, this,
          [stack, content, diffButton] {
            stack->setCurrentIndex(1);
            content->setChecked(false);
            diffButton->setChecked(true);
          });

  auto *buttons = new QHBoxLayout;
  buttons->addWidget(content);
  buttons->addWidget(diffButton);
  buttons->addStretch();

  auto *right = new QVBoxLayout;
  right->addLayout(buttons);
  right->addWidget(stack);
  auto *rightWidget = new QWidget(this);
  rightWidget->setLayout(right);

  auto *splitter = new QSplitter(this);
  splitter->addWidget(mList);
  splitter->addWidget(rightWidget);
  splitter->setStretchFactor(1, 1);

  mStatus = new QLabel(tr("Scanning history…"), this);
  auto *layout = new QVBoxLayout;
  layout->setContentsMargins(8, 8, 8, 8);
  layout->addWidget(splitter);
  layout->addWidget(mStatus);
  auto *central = new QWidget(this);
  central->setLayout(layout);
  setCentralWidget(central);

  connect(mList, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row >= 0)
      showEntry(row);
  });
  connect(mList, &QListWidget::itemSelectionChanged, this, [this, stack] {
    QList<QListWidgetItem *> selected = mList->selectedItems();
    if (selected.size() != 2)
      return;
    int a = mList->row(selected.at(0));
    int b = mList->row(selected.at(1));
    if (a < 0 || b < 0 || a >= mEntries.size() || b >= mEntries.size())
      return;
    showDiff(mEntries.at(qMin(a, b)), mEntries.at(qMax(a, b)));
    stack->setCurrentWidget(mDiff);
  });
  connect(mList, &QWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            QListWidgetItem *item = mList->itemAt(pos);
            if (!item || item->data(Qt::UserRole).toInt() < 0)
              return;
            int row = item->data(Qt::UserRole).toInt();
            if (row < 0 || row >= mEntries.size())
              return;
            const Entry &entry = mEntries.at(row);
            QMenu menu(this);
            menu.addAction(tr("Blame this revision"), this, [this, row] {
              mList->setCurrentRow(row);
              showEntry(row);
            });
            menu.addAction(tr("Save this revision as…"), this, [this, entry] {
              QString dest = QFileDialog::getSaveFileName(
                  this, tr("Save File"),
                  QFileInfo(entry.path).fileName());
              if (dest.isEmpty())
                return;
              git::Id id = entry.commit.tree().id(entry.path);
              git::Blob blob = mRepo.lookupBlob(id);
              if (!blob.isValid())
                return;
              QSaveFile file(dest);
              if (!file.open(QFile::WriteOnly))
                return;
              file.write(blob.content());
              file.commit();
            });
            menu.addAction(tr("Reset file to this revision"), this,
                           [this, entry] {
                             QMessageBox box(
                                 QMessageBox::Question, tr("Reset File"),
                                 tr("Replace the working copy of %1 with this "
                                    "revision?")
                                     .arg(entry.path),
                                 QMessageBox::Yes | QMessageBox::No, this);
                             if (box.exec() != QMessageBox::Yes)
                               return;
                             OperationLog::instance()->captureFiles(
                                 mRepo, {entry.path}, tr("Reset file"));
                             mRepo.checkout(entry.commit, nullptr, {entry.path},
                                            GIT_CHECKOUT_FORCE);
                           });
            menu.addAction(tr("Copy commit id"), this, [entry] {
              QApplication::clipboard()->setText(entry.commit.id().toString());
            });
            menu.exec(mList->mapToGlobal(pos));
          });

  mScanPath = path;
  if (git::Reference head = repo.head()) {
    mWalker = repo.walker(GIT_SORT_TIME);
    mWalker.push(head);
  } else {
    mScanning = false;
    mStatus->setText(tr("No commits"));
  }

  if (mScanning)
    QTimer::singleShot(0, this, &FileHistoryWindow::scanMore);
}

QString FileHistoryWindow::renamedFrom(const git::Commit &commit,
                                       const QString &path) const {
  QList<git::Commit> parents = commit.parents();
  if (parents.size() != 1)
    return {};
  git::Diff diff =
      mRepo.diffTrees(parents.first().tree(), commit.tree(), QString());
  if (!diff.isValid())
    return {};
  diff.findSimilar();
  for (int i = 0; i < diff.count(); ++i) {
    if (diff.status(i) != GIT_DELTA_RENAMED)
      continue;
    git::Patch patch = diff.patch(i);
    if (patch.isValid() && patch.name(git::Diff::NewFile) == path)
      return patch.name(git::Diff::OldFile);
  }
  return {};
}

bool FileHistoryWindow::touchesRange(const git::Commit &commit,
                                     const QString &path, int *start,
                                     int *end) const {
  if (*start < 0)
    return true;
  QList<git::Commit> parents = commit.parents();
  git::Tree old;
  if (!parents.isEmpty())
    old = parents.first().tree();
  git::Diff diff = mRepo.diffTrees(old, commit.tree(), path);
  if (!diff.isValid() || diff.count() == 0)
    return false;

  bool hit = false;
  int shift = 0;
  git::Patch patch = diff.patch(0);
  for (int h = 0; patch.isValid() && h < patch.count(); ++h) {
    const git_diff_hunk *hunk = patch.header_struct(h);
    if (!hunk)
      continue;
    int newStart = hunk->new_start;
    int newEnd = hunk->new_start + hunk->new_lines;
    if (newEnd < *start) {
      shift += static_cast<int>(hunk->old_lines) -
               static_cast<int>(hunk->new_lines);
    } else if (newStart <= *end) {
      hit = true;
    }
  }
  *start += shift;
  *end += shift;
  return hit;
}

void FileHistoryWindow::scanMore() {
  if (!mScanning || !mWalker.isValid()) {
    mStatus->setText(tr("%1 revisions").arg(mEntries.size()));
    return;
  }

  int scanned = 0;
  while (scanned < kScanBatch && mEntries.size() < kScanLimit) {
    git::Commit commit = mWalker.next(mScanPath, !mDirectory);
    if (!commit.isValid()) {
      mScanning = false;
      break;
    }
    ++scanned;
    int start = mLineStart;
    int end = mLineEnd;
    if (!touchesRange(commit, mScanPath, &start, &end)) {
      mLineStart = start;
      mLineEnd = end;
      continue;
    }
    mLineStart = start;
    mLineEnd = end;

    QString shown = dayKey(commit);
    if (mEntries.isEmpty() || dayKey(mEntries.last().commit) != shown) {
      auto *sep = new QListWidgetItem(shown);
      sep->setFlags(Qt::NoItemFlags);
      sep->setData(Qt::UserRole, -1);
      mList->addItem(sep);
    }

    QString label = tr("%1  %2  %3")
                        .arg(commit.author().date().toString("HH:mm"),
                             commit.shortId(),
                             commit.summary());
    auto *item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, mEntries.size());
    item->setToolTip(commit.message());
    mList->addItem(item);
    mEntries.append({commit, mScanPath});

    QString previous = renamedFrom(commit, mScanPath);
    if (!previous.isEmpty() && previous != mScanPath &&
        commit.parents().size() == 1) {
      mScanPath = previous;
      mWalker = mRepo.walker(GIT_SORT_TIME);
      mWalker.push(commit.parents().first());
      break;
    }
  }

  mStatus->setText(mScanning ? tr("Scanning history… %1 revisions")
                                   .arg(mEntries.size())
                             : tr("%1 revisions").arg(mEntries.size()));
  if (mList->currentRow() < 0 && mList->count() > 0)
    mList->setCurrentRow(1);

  if (mScanning)
    QTimer::singleShot(0, this, &FileHistoryWindow::scanMore);
}

void FileHistoryWindow::showEntry(int row) {
  QListWidgetItem *item = mList->item(row);
  if (!item)
    return;
  int index = item->data(Qt::UserRole).toInt();
  if (index < 0 || index >= mEntries.size())
    return;
  const Entry &entry = mEntries.at(index);
  git::Id id = entry.commit.tree().id(entry.path);
  git::Blob blob = mRepo.lookupBlob(id);
  if (blob.isValid())
    mEditor->load(entry.path, blob, entry.commit);

  QList<git::Commit> parents = entry.commit.parents();
  git::Tree old;
  if (!parents.isEmpty())
    old = parents.first().tree();
  git::Diff diff = mRepo.diffTrees(old, entry.commit.tree(), entry.path);
  mDiff->setPlainText(diff.isValid() ? QString::fromUtf8(diff.print())
                                     : QString());
}

void FileHistoryWindow::showDiff(const Entry &left, const Entry &right) {
  git::Diff diff =
      mRepo.diffTrees(left.commit.tree(), right.commit.tree(), right.path);
  mDiff->setPlainText(diff.isValid() ? QString::fromUtf8(diff.print())
                                     : QString());
}
