#include "InteractiveRebaseDialog.h"
#include "git/Branch.h"
#include "git/Reference.h"
#include "git/RevWalk.h"
#include "ui/OperationLog.h"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace {

const QStringList kActions = {"pick", "reword", "edit", "squash", "fixup",
                              "drop"};

QList<git::Commit> commitsAfter(const git::Repository &repo,
                                const git::Commit &onto) {
  QList<git::Commit> commits;
  if (!repo.head().isValid() || !onto.isValid())
    return commits;
  git::RevWalk walk = repo.walker(GIT_SORT_TIME | GIT_SORT_REVERSE);
  walk.push(repo.head());
  walk.hide(onto);
  while (git::Commit commit = walk.next())
    commits.append(commit);
  return commits;
}

} // namespace

void InteractiveRebaseDialog::start(const git::Repository &repo,
                                    const git::Commit &onto, QWidget *parent) {
  QList<git::Commit> commits = commitsAfter(repo, onto);
  if (commits.isEmpty()) {
    QMessageBox::information(parent, tr("Interactive Rebase"),
                             tr("There are no commits to rebase."));
    return;
  }
  auto *dialog = new InteractiveRebaseDialog(repo, onto, commits, parent);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
}

InteractiveRebaseDialog::InteractiveRebaseDialog(
    const git::Repository &repo, const git::Commit &onto,
    const QList<git::Commit> &commits, QWidget *parent)
    : QDialog(parent), mRepo(repo), mOnto(onto) {
  setWindowTitle(tr("Interactive Rebase onto %1").arg(onto.shortId()));
  resize(720, 480);

  mTable = new QTableWidget(commits.size(), 3, this);
  mTable->setHorizontalHeaderLabels({tr("Action"), tr("Commit"), tr("Message")});
  mTable->horizontalHeader()->setStretchLastSection(true);
  mTable->setSelectionBehavior(QAbstractItemView::SelectRows);
  mTable->setDragDropMode(QAbstractItemView::InternalMove);
  mTable->setDragEnabled(true);
  mTable->setAcceptDrops(true);
  mTable->setDropIndicatorShown(true);
  mTable->verticalHeader()->setVisible(false);

  for (int i = 0; i < commits.size(); ++i) {
    auto *action = new QTableWidgetItem("pick");
    action->setData(Qt::UserRole, commits.at(i).id().toString());
    mTable->setItem(i, 0, action);
    auto *id = new QTableWidgetItem(commits.at(i).shortId());
    id->setFlags(id->flags() & ~Qt::ItemIsEditable);
    mTable->setItem(i, 1, id);
    auto *summary = new QTableWidgetItem(commits.at(i).summary());
    mTable->setItem(i, 2, summary);
  }

  auto *hint = new QLabel(
      tr("Drag rows to reorder. Double-click the action to cycle it, or edit "
         "the message for reword."),
      this);
  hint->setWordWrap(true);

  auto *up = new QPushButton(tr("Up"), this);
  auto *down = new QPushButton(tr("Down"), this);
  auto *cycle = new QPushButton(tr("Cycle action"), this);
  connect(up, &QPushButton::clicked, this, [this] { moveCurrent(-1); });
  connect(down, &QPushButton::clicked, this, [this] { moveCurrent(1); });
  connect(cycle, &QPushButton::clicked, this, [this] {
    int row = mTable->currentRow();
    if (row < 0)
      return;
    QString action = mTable->item(row, 0)->text();
    int index = kActions.indexOf(action);
    mTable->item(row, 0)->setText(kActions.at((index + 1) % kActions.size()));
  });
  connect(mTable, &QTableWidget::itemDoubleClicked, this,
          [](QTableWidgetItem *item) {
            if (item && item->column() == 0) {
              int index = kActions.indexOf(item->text());
              item->setText(kActions.at((index + 1) % kActions.size()));
            }
          });

  auto *rowButtons = new QHBoxLayout;
  rowButtons->addWidget(up);
  rowButtons->addWidget(down);
  rowButtons->addWidget(cycle);
  rowButtons->addStretch();

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Start rebase"));
  connect(buttons, &QDialogButtonBox::accepted, this,
          &InteractiveRebaseDialog::apply);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(hint);
  layout->addWidget(mTable);
  layout->addLayout(rowButtons);
  layout->addWidget(buttons);
}

void InteractiveRebaseDialog::moveCurrent(int delta) {
  int row = mTable->currentRow();
  int target = row + delta;
  if (row < 0 || target < 0 || target >= mTable->rowCount())
    return;
  for (int column = 0; column < mTable->columnCount(); ++column) {
    QTableWidgetItem *current = mTable->takeItem(row, column);
    QTableWidgetItem *other = mTable->takeItem(target, column);
    mTable->setItem(target, column, current);
    mTable->setItem(row, column, other);
  }
  mTable->setCurrentCell(target, 0);
}

void InteractiveRebaseDialog::apply() {
  QString git = QStandardPaths::findExecutable("git");
  if (git.isEmpty()) {
    QMessageBox::warning(this, tr("Interactive Rebase"),
                         tr("git was not found on PATH."));
    return;
  }

  QString path = mRepo.workdir().path();
  QProcess check;
  check.start(git, {"-C", path, "merge-tree", "--write-tree", mOnto.id().toString(),
                    "HEAD"});
  check.waitForFinished(15000);
  if (check.exitCode() != 0 && check.exitStatus() == QProcess::NormalExit) {
    QString output = QString::fromUtf8(check.readAllStandardOutput());
    if (output.contains("CONFLICT") ||
        QString::fromUtf8(check.readAllStandardError()).contains("CONFLICT")) {
      if (QMessageBox::question(
              this, tr("Interactive Rebase"),
              tr("This rebase looks like it will conflict. Continue?")) !=
          QMessageBox::Yes)
        return;
    }
  }

  QString backup =
      QString("backup/rebase-%1").arg(QDateTime::currentDateTime().toString(
          "yyyyMMdd-HHmmss"));
  if (git::Commit head = mRepo.head().target())
    mRepo.createBranch(backup, head, true);

  QTemporaryDir dir;
  dir.setAutoRemove(false);
  QString todoPath = dir.filePath("todo");
  QString scriptPath = dir.filePath("editor.sh");
  QFile todo(todoPath);
  if (!todo.open(QFile::WriteOnly | QFile::Truncate))
    return;
  for (int row = 0; row < mTable->rowCount(); ++row) {
    QString action = mTable->item(row, 0)->text().trimmed();
    if (!kActions.contains(action))
      action = "pick";
    if (action == "drop")
      continue;
    QString id = mTable->item(row, 0)->data(Qt::UserRole).toString();
    QString message = mTable->item(row, 2)->text().replace('\n', ' ');
    QString quoted = "\"" + QString(message).replace("\"", "\\\"") + "\"";
    if (action == "reword") {
      todo.write(QString("pick %1 %2\nexec git commit --amend -m %3\n")
                     .arg(id, message, quoted)
                     .toUtf8());
    } else {
      todo.write(QString("%1 %2 %3\n").arg(action, id, message).toUtf8());
    }
  }
  todo.close();

  QFile script(scriptPath);
  if (!script.open(QFile::WriteOnly | QFile::Truncate))
    return;
  script.write("#!/bin/sh\ncp \"$GITTYUP_REBASE_TODO\" \"$1\"\n");
  script.close();
  script.setPermissions(QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner);

  OperationLog::instance()->capture(mRepo, tr("Interactive rebase"));

  QProcess process;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("GITTYUP_REBASE_TODO", todoPath);
  env.insert("GIT_SEQUENCE_EDITOR", scriptPath);
  env.insert("GIT_EDITOR", "true");
  process.setProcessEnvironment(env);
  process.setWorkingDirectory(path);
  process.start(git, {"-C", path, "rebase", "-i", mOnto.id().toString()});
  process.waitForFinished(300000);

  QString err = QString::fromUtf8(process.readAllStandardError());
  if (process.exitCode() != 0) {
    QMessageBox::warning(
        this, tr("Interactive Rebase"),
        tr("Rebase did not finish cleanly.\n\nA backup branch %1 was created.\n\n%2")
            .arg(backup, err.left(2000)));
  } else {
    QMessageBox::information(
        this, tr("Interactive Rebase"),
        tr("Rebase finished. Backup branch: %1").arg(backup));
  }
  accept();
}
