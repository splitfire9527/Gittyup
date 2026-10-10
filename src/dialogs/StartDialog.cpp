//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "StartDialog.h"
#include "AccountDialog.h"
#include "CloneDialog.h"
#include "IconLabel.h"
#include "app/Application.h"
#include "conf/RecentRepositories.h"
#include "conf/RecentRepository.h"
#include "conf/RepositoryGroups.h"
#include "host/Accounts.h"
#include "host/Repository.h"
#include "ui/Footer.h"
#include "ui/MainWindow.h"
#include "ui/ProgressIndicator.h"
#include "ui/RepoView.h"
#include "ui/TabWidget.h"
#include <QAbstractItemModel>
#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMenu>
#include <QMultiMap>
#include <QPushButton>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

const QString kSubtitleFmt = "<h4 style='margin-top: 0px; color: gray'>%2</h4>";

const QString kGeometryKey = "geometry";
const QString kStartGroup = "start";

const QString kCloudIcon = ":/cloud.png";

enum Role { KindRole = Qt::UserRole, AccountRole, RepositoryRole };

class RepoModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Row { Clone, Open, Init };

  RepoModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {
    RecentRepositories *repos = RecentRepositories::instance();
    connect(repos, &RecentRepositories::repositoryAboutToBeAdded, this,
            &RepoModel::beginResetModel);
    connect(repos, &RecentRepositories::repositoryAdded, this,
            &RepoModel::endResetModel);
    connect(repos, &RecentRepositories::repositoryAboutToBeRemoved, this,
            &RepoModel::beginResetModel);
    connect(repos, &RecentRepositories::repositoryRemoved, this,
            &RepoModel::endResetModel);

    RepositoryGroups *groups = RepositoryGroups::instance();
    connect(groups, &RepositoryGroups::groupsChanged, this, [this] {
      beginResetModel();
      endResetModel();
    });
  }

  void setShowFullPath(bool enabled) {
    beginResetModel();
    mShowFullPath = enabled;
    endResetModel();
  }

  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override {
    if (row < 0 || column != 0)
      return QModelIndex();
    if (!parent.isValid()) {
      if (isEmpty())
        return createIndex(row, column);
      RepositoryGroups *groups = RepositoryGroups::instance();
      if (row < groups->count())
        return createIndex(row, column, groups->at(row));
      return createIndex(row, column);
    }

    QObject *ptr = static_cast<QObject *>(parent.internalPointer());
    if (RepositoryGroup *group = qobject_cast<RepositoryGroup *>(ptr)) {
      if (row >= group->count())
        return QModelIndex();
      return createIndex(row, column, group->repository(row));
    }

    if (!ptr && parent.row() == ungroupedRow()) {
      QList<RecentRepository *> repos = ungrouped();
      if (row >= repos.size())
        return QModelIndex();
      return createIndex(row, column, repos.at(row));
    }
    return QModelIndex();
  }

  QModelIndex parent(const QModelIndex &child) const override {
    QObject *ptr = static_cast<QObject *>(child.internalPointer());
    if (GroupRepository *repo = qobject_cast<GroupRepository *>(ptr)) {
      RepositoryGroups *groups = RepositoryGroups::instance();
      return createIndex(groups->indexOf(repo->group()), 0, repo->group());
    }
    if (qobject_cast<RecentRepository *>(ptr))
      return createIndex(ungroupedRow(), 0);
    return QModelIndex();
  }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    if (!parent.isValid()) {
      if (isEmpty())
        return 3;
      int rows = RepositoryGroups::instance()->count();
      if (!ungrouped().isEmpty())
        ++rows;
      return rows;
    }

    QObject *ptr = static_cast<QObject *>(parent.internalPointer());
    if (RepositoryGroup *group = qobject_cast<RepositoryGroup *>(ptr))
      return group->count();
    if (!ptr && parent.row() == ungroupedRow())
      return ungrouped().size();
    return 0;
  }

  int columnCount(const QModelIndex &parent = QModelIndex()) const override {
    Q_UNUSED(parent)
    return 1;
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    if (!index.isValid())
      return QVariant();

    QObject *ptr = static_cast<QObject *>(index.internalPointer());
    if (GroupRepository *repo = qobject_cast<GroupRepository *>(ptr)) {
      if (role == Qt::DisplayRole)
        return mShowFullPath ? repo->path() : QFileInfo(repo->path()).fileName();
      if (role == Qt::UserRole || role == Qt::ToolTipRole)
        return repo->path();
      return QVariant();
    }
    if (RecentRepository *repo = qobject_cast<RecentRepository *>(ptr)) {
      if (role == Qt::DisplayRole)
        return mShowFullPath ? repo->gitpath() : repo->name();
      if (role == Qt::UserRole || role == Qt::ToolTipRole)
        return repo->gitpath();
      return QVariant();
    }
    if (RepositoryGroup *group = qobject_cast<RepositoryGroup *>(ptr)) {
      if (role == Qt::DisplayRole)
        return group->name();
      if (role == Qt::FontRole) {
        QFont font;
        font.setBold(true);
        return font;
      }
      return QVariant();
    }

    if (isEmpty()) {
      if (role == Qt::DisplayRole) {
        switch (index.row()) {
          case Clone:
            return tr("Clone Repository");
          case Open:
            return tr("Open Existing Repository");
          case Init:
            return tr("Initialize New Repository");
        }
      }
      if (role == Qt::DecorationRole) {
        switch (index.row()) {
          case Clone:
            return QIcon(":/clone.png");
          case Open:
            return QIcon(":/open.png");
          case Init:
            return QIcon(":/new.png");
        }
      }
      return QVariant();
    }

    if (role == Qt::DisplayRole && index.row() == ungroupedRow())
      return tr("Ungrouped");
    if (role == Qt::FontRole && index.row() == ungroupedRow()) {
      QFont font;
      font.setBold(true);
      return font;
    }
    return QVariant();
  }

  Qt::ItemFlags flags(const QModelIndex &index) const override {
    Qt::ItemFlags itemFlags = QAbstractItemModel::flags(index);
    if (isEmpty())
      return itemFlags & ~Qt::ItemIsSelectable;
    if (!index.data(Qt::UserRole).toString().isEmpty())
      itemFlags |= Qt::ItemIsDragEnabled;
    itemFlags |= Qt::ItemIsDropEnabled;
    return itemFlags;
  }

  Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }

  QStringList mimeTypes() const override {
    return {"application/x-gittyup-repo"};
  }

  QMimeData *mimeData(const QModelIndexList &indexes) const override {
    auto *data = new QMimeData;
    QStringList paths;
    for (const QModelIndex &index : indexes) {
      QString path = index.data(Qt::UserRole).toString();
      if (!path.isEmpty())
        paths.append(path);
    }
    data->setData("application/x-gittyup-repo", paths.join('\n').toUtf8());
    return data;
  }

  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                    int column, const QModelIndex &parent) override {
    Q_UNUSED(action)
    Q_UNUSED(row)
    Q_UNUSED(column)
    if (!data)
      return false;
    const QStringList paths =
        QString::fromUtf8(data->data("application/x-gittyup-repo"))
            .split('\n', Qt::SkipEmptyParts);
    if (paths.isEmpty())
      return false;

    RepositoryGroups *groups = RepositoryGroups::instance();
    RepositoryGroup *group = nullptr;
    QObject *ptr = static_cast<QObject *>(parent.internalPointer());
    if (auto *repo = qobject_cast<GroupRepository *>(ptr))
      group = repo->group();
    else if (auto *named = qobject_cast<RepositoryGroup *>(ptr))
      group = named;
    else if (parent.parent().isValid())
      group = qobject_cast<RepositoryGroup *>(
          static_cast<QObject *>(parent.parent().internalPointer()));

    for (const QString &path : paths) {
      if (group)
        groups->addPath(group, path);
      else if (RepositoryGroup *current = groups->groupForPath(path))
        current->removePath(path);
    }
    return true;
  }

private:
  bool isEmpty() const {
    return RecentRepositories::instance()->count() == 0 &&
           RepositoryGroups::instance()->count() == 0;
  }

  QList<RecentRepository *> ungrouped() const {
    QList<RecentRepository *> result;
    RecentRepositories *repos = RecentRepositories::instance();
    RepositoryGroups *groups = RepositoryGroups::instance();
    for (int i = 0; i < repos->count(); ++i) {
      RecentRepository *repo = repos->repository(i);
      if (!groups->containsPath(repo->gitpath()))
        result.append(repo);
    }
    return result;
  }

  int ungroupedRow() const { return RepositoryGroups::instance()->count(); }

  bool mShowFullPath = false;
};

class HostModel : public QAbstractItemModel {
  Q_OBJECT

public:
  HostModel(QStyle *style, QObject *parent = nullptr)
      : QAbstractItemModel(parent),
        mErrorIcon(style->standardIcon(QStyle::SP_MessageBoxCritical)) {
    Accounts *accounts = Accounts::instance();
    connect(accounts, &Accounts::accountAboutToBeAdded, this,
            &HostModel::beginResetModel);
    connect(accounts, &Accounts::accountAdded, this, &HostModel::endResetModel);
    connect(accounts, &Accounts::accountAboutToBeRemoved, this,
            &HostModel::beginResetModel);
    connect(accounts, &Accounts::accountRemoved, this,
            &HostModel::endResetModel);

    connect(accounts, &Accounts::progress, this, [this](int accountIndex) {
      QModelIndex idx = index(0, 0, index(accountIndex, 0));
      emit dataChanged(idx, idx, {Qt::DisplayRole});
    });
    connect(accounts, &Accounts::started, this, [this](int accountIndex) {
      beginResetModel();
      endResetModel();
    });
    connect(accounts, &Accounts::finished, this, [this](int accountIndex) {
      beginResetModel();
      endResetModel();
    });

    connect(accounts, &Accounts::repositoryAboutToBeAdded, this,
            &HostModel::beginResetModel);
    connect(accounts, &Accounts::repositoryAdded, this,
            &HostModel::endResetModel);
    connect(accounts, &Accounts::repositoryPathChanged, this,
            [this](int accountIndex, int repoIndex) {
              QModelIndex idx = index(repoIndex, 0, index(accountIndex, 0));
              emit dataChanged(idx, idx, {Qt::DisplayRole});
            });
  }

  void setShowFullName(bool enabled) {
    beginResetModel();
    mShowFullName = enabled;
    endResetModel();
  }

  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override {
    bool id = (!parent.isValid() || parent.internalId());
    return createIndex(row, column, !id ? parent.row() + 1 : 0);
  }

  QModelIndex parent(const QModelIndex &index) const override {
    quintptr id = index.internalId();
    return !id ? QModelIndex() : createIndex(id - 1, 0);
  }

  int rowCount(const QModelIndex &parent = QModelIndex()) const override {
    // no accounts
    Accounts *accounts = Accounts::instance();
    if (accounts->count() <= 0)
      return !parent.isValid() ? 4 : 0;

    // account
    if (!parent.isValid())
      return accounts->count();

    if (parent.internalId())
      return 0;

    // repos
    Account *account = accounts->account(parent.row());
    int count = account->repositoryCount();
    if (count > 0)
      return count;

    AccountError *error = account->error();
    AccountProgress *progress = account->progress();
    return (error->isValid() || progress->isValid()) ? 1 : 0;
  }

  int columnCount(const QModelIndex &parent = QModelIndex()) const override {
    return 1;
  }

  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override {
    // no accounts
    Accounts *accounts = Accounts::instance();
    if (accounts->count() <= 0) {
      Account::Kind kind = static_cast<Account::Kind>(index.row());
      switch (role) {
        case Qt::DisplayRole:
          return Account::name(kind);
        case Qt::DecorationRole:
          return Account::icon(kind);
        case KindRole:
          return kind;
      }

      return QVariant();
    }

    // account
    quintptr id = index.internalId();
    if (!id) {
      Account *account = accounts->account(index.row());
      switch (role) {
        case Qt::DisplayRole:
          return account->username();
        case Qt::DecorationRole:
          return Account::icon(account->kind());
        case KindRole:
          return account->kind();
        case AccountRole:
          return QVariant::fromValue(account);
      }

      return QVariant();
    }

    // error
    Account *account = accounts->account(id - 1);
    AccountError *error = account->error();
    if (error->isValid()) {
      switch (role) {
        case Qt::DisplayRole:
          return error->text();
        case Qt::DecorationRole:
          return mErrorIcon;
        case Qt::ToolTipRole:
          return error->detailedText();
      }

      return QVariant();
    }

    // progress
    if (account->repositoryCount() <= 0) {
      switch (role) {
        case Qt::DisplayRole:
          return tr("Connecting");

        case Qt::DecorationRole:
          return account->progress()->value();

        case Qt::FontRole: {
          QFont font = static_cast<QWidget *>(QObject::parent())->font();
          font.setItalic(true);
          return font;
        }

        default:
          return QVariant();
      }
    }

    // repo
    int row = index.row();
    Repository *repo = account->repository(row);
    switch (role) {
      case Qt::DisplayRole:
        return mShowFullName ? repo->fullName() : repo->name();

      case Qt::DecorationRole: {
        QString path = account->repositoryPath(row);
        return path.isEmpty() ? QIcon(kCloudIcon) : QIcon();
      }

      case KindRole:
        return account->kind();
      case RepositoryRole:
        return QVariant::fromValue(repo);
    }

    return QVariant();
  }

  Qt::ItemFlags flags(const QModelIndex &index) const override {
    Qt::ItemFlags flags = QAbstractItemModel::flags(index);
    if (Accounts::instance()->count() <= 0)
      flags &= ~Qt::ItemIsSelectable;
    return flags;
  }

private:
  bool mShowFullName = false;
  QIcon mErrorIcon;
};

class ProgressDelegate : public QStyledItemDelegate {
public:
  ProgressDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    // Draw background.
    QStyledItemDelegate::paint(painter, opt, index);

    // Draw busy indicator.
    QVariant progress = index.data(Qt::DecorationRole);
    if (!progress.canConvert<int>())
      return;

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    QStyle::SubElement se = QStyle::SE_ItemViewItemDecoration;
    QRect rect = style->subElementRect(se, &opt, opt.widget);
    ProgressIndicator::paint(painter, rect, "#808080", progress.toInt());
  }

protected:
  void initStyleOption(QStyleOptionViewItem *option,
                       const QModelIndex &index) const override {
    QStyledItemDelegate::initStyleOption(option, index);
    if (index.data(Qt::DecorationRole).canConvert<int>()) {
      option->decorationSize = ProgressIndicator::size();
    } else if (index.data(RepositoryRole).isValid()) {
      option->features |= QStyleOptionViewItem::HasDecoration;
      option->decorationSize = QSize(20, 20);
    }
  }
};

QStringList pathsUnder(const QModelIndex &index) {
  QString path = index.data(Qt::UserRole).toString();
  if (!path.isEmpty())
    return {path};

  QStringList paths;
  if (!index.isValid() || !index.model())
    return paths;
  const QAbstractItemModel *model = index.model();
  for (int i = 0; i < model->rowCount(index); ++i) {
    QString child = model->index(i, 0, index).data(Qt::UserRole).toString();
    if (!child.isEmpty())
      paths.append(child);
  }
  return paths;
}

} // namespace

StartDialog::StartDialog(QWidget *parent) : QDialog(parent) {
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(tr("Choose Repository"));

  QIcon icon(":/Gittyup.iconset/icon_128x128.png");
  IconLabel *iconLabel = new IconLabel(icon, 128, 128, this);

  QIcon title(":/logo-type_light@2x.png");
  IconLabel *titleLabel = new IconLabel(title, 163, 38, this);

  QString subtitleText = kSubtitleFmt.arg(tr("Understand your history!"));
  QLabel *subtitle = new QLabel(subtitleText, this);
  subtitle->setAlignment(Qt::AlignHCenter);

  QVBoxLayout *left = new QVBoxLayout;
  left->addWidget(iconLabel);
  left->addWidget(titleLabel);
  left->addWidget(subtitle);
  left->addStretch();

  mRepoList = new QTreeView(this);
  mRepoList->setHeaderHidden(true);
  mRepoList->setIconSize(QSize(32, 32));
  mRepoList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  mRepoList->setDragEnabled(true);
  mRepoList->setAcceptDrops(true);
  mRepoList->setDropIndicatorShown(true);
  mRepoList->setDragDropMode(QAbstractItemView::DragDrop);
  mRepoList->setDefaultDropAction(Qt::MoveAction);
  connect(mRepoList, &QTreeView::clicked, this,
          [this](const QModelIndex &index) {
            bool empty = RecentRepositories::instance()->count() == 0 &&
                         RepositoryGroups::instance()->count() == 0;
            if (!empty || index.parent().isValid() ||
                index.data(Qt::UserRole).isValid())
              return;
            switch (index.row()) {
              case RepoModel::Clone:
                mClone->trigger();
                break;
              case RepoModel::Open:
                mOpen->trigger();
                break;
              case RepoModel::Init:
                mInit->trigger();
                break;
            }
          });

  connect(mRepoList, &QTreeView::doubleClicked, this,
          [this](const QModelIndex &index) {
            if (!pathsUnder(index).isEmpty())
              accept();
          });

  RepoModel *repoModel = new RepoModel(mRepoList);
  mRepoList->setModel(repoModel);
  connect(repoModel, &RepoModel::modelReset, this, [this] {
    mRepoList->expandAll();
    mRepoList->setCurrentIndex(mRepoList->model()->index(0, 0));
  });

  mRepoFooter = new Footer(mRepoList);
  connect(mRepoFooter, &Footer::minusClicked, this, [this] {
    // Sort selection in reverse order.
    QModelIndexList indexes = mRepoList->selectionModel()->selectedIndexes();
    std::sort(indexes.begin(), indexes.end(),
              [](const QModelIndex &lhs, const QModelIndex &rhs) {
                return rhs.row() < lhs.row();
              });

    QList<RepositoryGroup *> groups;
    QStringList paths;
    for (const QModelIndex &index : indexes) {
      if (auto *group = qobject_cast<RepositoryGroup *>(
              static_cast<QObject *>(index.internalPointer())))
        groups.append(group);
      else if (!index.data(Qt::UserRole).toString().isEmpty())
        paths.append(index.data(Qt::UserRole).toString());
    }
    for (const QString &path : paths) {
      if (RepositoryGroup *group = RepositoryGroups::instance()->groupForPath(path))
        group->removePath(path);
      else {
        RecentRepositories *repos = RecentRepositories::instance();
        for (int i = repos->count() - 1; i >= 0; --i) {
          if (QDir(repos->repository(i)->gitpath()) == QDir(path))
            repos->remove(i);
        }
      }
    }
    for (RepositoryGroup *group : groups)
      RepositoryGroups::instance()->remove(group);
  });

  QMenu *repoPlusMenu = new QMenu(this);
  mRepoFooter->setPlusMenu(repoPlusMenu);

  mClone = repoPlusMenu->addAction(tr("Clone Repository"));
  connect(mClone, &QAction::triggered, this, [this] {
    CloneDialog *dialog = new CloneDialog(CloneDialog::Clone, this);
    connect(dialog, &CloneDialog::accepted, this, [this, dialog] {
      if (MainWindow *window = openWindow(dialog->path()))
        window->currentView()->addLogEntry(dialog->message(),
                                           dialog->messageTitle());
    });
    dialog->open();
  });

  mOpen = repoPlusMenu->addAction(tr("Open Existing Repository"));
  connect(mOpen, &QAction::triggered, this, [this] {
    // FIXME: Filter out non-git dirs.
    QFileDialog *dialog =
        new QFileDialog(this, tr("Open Repository"), QDir::homePath());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setFileMode(QFileDialog::Directory);
    dialog->setOption(QFileDialog::ShowDirsOnly);
    connect(dialog, &QFileDialog::fileSelected, this, &StartDialog::openWindow);
    dialog->open();
  });

  QAction *newGroup = repoPlusMenu->addAction(tr("New Group"));
  connect(newGroup, &QAction::triggered, this, [this] {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Group"), tr("Name:"),
                                         QLineEdit::Normal, QString(), &ok);
    if (ok && !name.isEmpty())
      RepositoryGroups::instance()->add(name);
  });

  mInit = repoPlusMenu->addAction(tr("Initialize New Repository"));
  connect(mInit, &QAction::triggered, this, [this] {
    CloneDialog *dialog = new CloneDialog(CloneDialog::Init, this);
    connect(dialog, &CloneDialog::accepted, this, [this, dialog] {
      if (MainWindow *window = openWindow(dialog->path()))
        window->currentView()->addLogEntry(dialog->message(),
                                           dialog->messageTitle());
    });
    dialog->open();
  });

  QMenu *repoContextMenu = new QMenu(this);
  mRepoFooter->setContextMenu(repoContextMenu);

  QAction *renameGroup = repoContextMenu->addAction(tr("Rename Group"));
  connect(renameGroup, &QAction::triggered, this, [this] {
    QModelIndex index = mRepoList->currentIndex();
    auto *group = qobject_cast<RepositoryGroup *>(
        static_cast<QObject *>(index.internalPointer()));
    if (!group)
      return;
    bool ok = false;
    QString name =
        QInputDialog::getText(this, tr("Rename Group"), tr("Name:"),
                              QLineEdit::Normal, group->name(), &ok);
    if (ok && !name.isEmpty())
      group->setName(name);
  });

  QAction *openGroup = repoContextMenu->addAction(tr("Open All in Group"));
  connect(openGroup, &QAction::triggered, this, [this] {
    QModelIndex index = mRepoList->currentIndex();
    if (!pathsUnder(index).isEmpty())
      accept();
  });

  QAction *clear = repoContextMenu->addAction(tr("Clear All"));
  connect(clear, &QAction::triggered,
          [] { RecentRepositories::instance()->clear(); });

  QSettings settings;
  QAction *showFullPath = repoContextMenu->addAction(tr("Show Full Path"));
  bool recentChecked = settings.value("start/recent/fullpath").toBool();
  showFullPath->setCheckable(true);
  showFullPath->setChecked(recentChecked);
  repoModel->setShowFullPath(recentChecked);
  connect(showFullPath, &QAction::triggered, this, [repoModel](bool checked) {
    QSettings().setValue("start/recent/fullpath", checked);
    repoModel->setShowFullPath(checked);
  });

  QAction *filter = repoContextMenu->addAction(tr("Filter Non-existent Paths"));
  filter->setCheckable(true);
  filter->setChecked(settings.value("recent/filter", true).toBool());
  connect(filter, &QAction::triggered,
          [](bool checked) { QSettings().setValue("recent/filter", checked); });

  QVBoxLayout *middle = new QVBoxLayout;
  middle->setSpacing(0);
  middle->addWidget(new QLabel(tr("Repositories:"), this));
  middle->addSpacing(8); // FIXME: Query style?
  middle->addWidget(mRepoList);
  middle->addWidget(mRepoFooter);

  mHostTree = new QTreeView(this);
  mHostTree->setHeaderHidden(true);
  mHostTree->setExpandsOnDoubleClick(false);
  mHostTree->setIconSize(QSize(32, 32));
  mHostTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  connect(mHostTree, &QTreeView::clicked, this,
          [this](const QModelIndex &index) {
            int rows = mHostTree->model()->rowCount(index);
            if (!rows && !index.data(RepositoryRole).isValid())
              edit(index);
          });

  connect(mHostTree, &QTreeView::doubleClicked, this,
          [this](const QModelIndex &index) {
            QModelIndex parent = index.parent();
            if (parent.isValid()) {
              Account *account = parent.data(AccountRole).value<Account *>();
              if (!account->repositoryPath(index.row()).isEmpty()) {
                accept();
                return;
              }
            }

            edit(index);
          });

  HostModel *hostModel = new HostModel(style(), mHostTree);
  mHostTree->setModel(hostModel);
  connect(hostModel, &QAbstractItemModel::modelReset, this, [this] {
    QModelIndex index = mHostTree->model()->index(0, 0);
    mHostTree->setRootIsDecorated(index.data(AccountRole).isValid());
    mHostTree->expandAll();
  });

  mHostTree->setItemDelegate(new ProgressDelegate(this));

  mHostFooter = new Footer(mHostTree);
  connect(mHostFooter, &Footer::plusClicked, this, [this] { edit(); });
  connect(mHostFooter, &Footer::minusClicked, this, &StartDialog::remove);

  QMenu *hostContextMenu = new QMenu(this);
  mHostFooter->setContextMenu(hostContextMenu);

  QAction *refresh = hostContextMenu->addAction(tr("Refresh"));
  connect(refresh, &QAction::triggered, this, [] {
    Accounts *accounts = Accounts::instance();
    for (int i = 0; i < accounts->count(); ++i)
      accounts->account(i)->connect();
  });

  QAction *showFullName = hostContextMenu->addAction(tr("Show Full Name"));
  bool remoteChecked = QSettings().value("start/remote/fullname").toBool();
  showFullName->setCheckable(true);
  showFullName->setChecked(remoteChecked);
  hostModel->setShowFullName(remoteChecked);
  connect(showFullName, &QAction::triggered, this, [hostModel](bool checked) {
    QSettings().setValue("start/remote/fullname", checked);
    hostModel->setShowFullName(checked);
  });

  // Clear the other list when this selection changes.
  QItemSelectionModel *repoSelModel = mRepoList->selectionModel();
  connect(repoSelModel, &QItemSelectionModel::selectionChanged, this, [this] {
    if (!mRepoList->selectionModel()->selectedIndexes().isEmpty())
      mHostTree->clearSelection();
    updateButtons();
  });

  // Clear the other list when this selection changes.
  QItemSelectionModel *hostSelModel = mHostTree->selectionModel();
  connect(hostSelModel, &QItemSelectionModel::selectionChanged, this, [this] {
    if (!mHostTree->selectionModel()->selectedIndexes().isEmpty())
      mRepoList->clearSelection();
    updateButtons();
  });

  QVBoxLayout *right = new QVBoxLayout;
  right->setSpacing(0);
  right->addWidget(new QLabel(tr("Remote:"), this));
  right->addSpacing(8); // FIXME: Query style?
  right->addWidget(mHostTree);
  right->addWidget(mHostFooter);

  QHBoxLayout *top = new QHBoxLayout;
  top->addLayout(left);
  top->addSpacing(12);
  top->addLayout(middle);
  top->addSpacing(12);
  top->addLayout(right);

  QDialogButtonBox::StandardButtons buttons =
      QDialogButtonBox::Open | QDialogButtonBox::Cancel;
  mButtonBox = new QDialogButtonBox(buttons, this);
  connect(mButtonBox, &QDialogButtonBox::accepted, this, &StartDialog::accept);
  connect(mButtonBox, &QDialogButtonBox::rejected, this, &StartDialog::reject);

  QString text = tr("View Getting Started Video");
  QPushButton *help = mButtonBox->addButton(text, QDialogButtonBox::ResetRole);
  connect(help, &QPushButton::clicked, [] {
    QDesktopServices::openUrl(QUrl("https://gitahead.com/#tutorials"));
  });

  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(mButtonBox);
}

void StartDialog::accept() {
  QModelIndexList repoIndexes = mRepoList->selectionModel()->selectedIndexes();
  QModelIndexList hostIndexes = mHostTree->selectionModel()->selectedIndexes();

  QStringList paths;
  QSet<QString> seen;
  for (const QModelIndex &index : repoIndexes) {
    for (const QString &path : pathsUnder(index)) {
      if (seen.contains(path))
        continue;
      seen.insert(path);
      paths.append(path);
    }
  }

  QModelIndexList uncloned;
  for (const QModelIndex &index : hostIndexes) {
    QModelIndex parent = index.parent();
    if (parent.isValid()) {
      Account *account = parent.data(AccountRole).value<Account *>();
      QString path = account->repositoryPath(index.row());
      if (path.isEmpty()) {
        uncloned.append(index);
      } else {
        paths.append(path);
      }
    }
  }

  // Clone the first repo.
  if (!uncloned.isEmpty()) {
    edit(uncloned.first());
    return;
  }

  // FIXME: Fail if none of the windows were able to be opened?
  QDialog::accept();

  if (paths.isEmpty())
    return;

  // Open a new window for the first valid repo.
  MainWindow *window = MainWindow::open(paths.takeFirst());
  while (!window && !paths.isEmpty())
    window = MainWindow::open(paths.takeFirst());

  if (!window)
    return;

  // Add the remainder as tabs.
  for (const QString &path : paths)
    window->addTab(path);
}

StartDialog *StartDialog::openSharedInstance() {
  static QPointer<StartDialog> dialog;
  if (dialog) {
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    return dialog;
  }

  dialog = new StartDialog;
  dialog->show();
  return dialog;
}

void StartDialog::showEvent(QShowEvent *event) {
  // Restore geometry.
  QSettings settings;
  settings.beginGroup(kStartGroup);
  if (settings.contains(kGeometryKey))
    restoreGeometry(settings.value(kGeometryKey).toByteArray());
  settings.endGroup();

  QDialog::showEvent(event);
}

void StartDialog::hideEvent(QHideEvent *event) {
  QSettings settings;
  settings.beginGroup(kStartGroup);
  settings.setValue(kGeometryKey, saveGeometry());
  settings.endGroup();

  QDialog::hideEvent(event);
}

void StartDialog::updateButtons() {
  QModelIndexList repoIndexes = mRepoList->selectionModel()->selectedIndexes();
  QModelIndexList hostIndexes = mHostTree->selectionModel()->selectedIndexes();

  // Update dialog Open button.
  bool clone = false;
  QPushButton *open = mButtonBox->button(QDialogButtonBox::Open);
  open->setEnabled(!repoIndexes.isEmpty() || !hostIndexes.isEmpty());
  for (const QModelIndex &index : hostIndexes) {
    QModelIndex parent = index.parent();
    if (!parent.isValid()) {
      open->setEnabled(false);
    } else {
      Account *account = parent.data(AccountRole).value<Account *>();
      if (nullptr != index.data(RepositoryRole).value<Repository *>()) {
        if (account->repositoryPath(index.row()).isEmpty())
          clone = true;
      } else {
        open->setEnabled(false);
      }
    }
  }

  open->setText((clone && open->isEnabled()) ? tr("Clone") : tr("Open"));

  // Update repo list footer buttons.
  mRepoFooter->setMinusEnabled(!repoIndexes.isEmpty());

  // Update host list footer buttons.
  mHostFooter->setMinusEnabled(false);
  if (!hostIndexes.isEmpty()) {
    QModelIndex index = hostIndexes.first();
    Account *account = index.data(AccountRole).value<Account *>();

    QString repoPath;
    QModelIndex parent = index.parent();
    if (parent.isValid()) {
      Account *account = parent.data(AccountRole).value<Account *>();
      repoPath = account->repositoryPath(index.row());
    }

    mHostFooter->setMinusEnabled(account || !repoPath.isEmpty());
  }
}

void StartDialog::edit(const QModelIndex &index) {
  // account
  if (!index.isValid() || !index.parent().isValid()) {
    Account *account = nullptr;
    if (index.isValid())
      account = index.data(AccountRole).value<Account *>();

    AccountDialog *dialog = new AccountDialog(account, this);
    if (!account && index.isValid())
      dialog->setKind(index.data(KindRole).value<Account::Kind>());

    dialog->open();
    return;
  }

  // repo
  Repository *repo = index.data(RepositoryRole).value<Repository *>();
  if (!repo)
    return;

  CloneDialog *dialog = new CloneDialog(CloneDialog::Clone, this, repo);
  connect(dialog, &CloneDialog::accepted, this, [this, index, dialog] {
    // Set local path.
    Account *account = Accounts::instance()->account(index.parent().row());
    account->setRepositoryPath(index.row(), dialog->path());

    // Open the repo.
    QCoreApplication::processEvents();
    accept();
  });

  dialog->open();
}

void StartDialog::remove() {
  QModelIndexList indexes = mHostTree->selectionModel()->selectedIndexes();
  Q_ASSERT(!indexes.isEmpty());

  // account
  QModelIndex index = indexes.first();
  if (!index.parent().isValid()) {
    Account::Kind kind = index.data(KindRole).value<Account::Kind>();
    QString name = index.data(Qt::DisplayRole).toString();
    QString fmt =
        tr("<p>Are you sure you want to remove the %1 account for '%2'?</p>"
           "<p>Only the account association will be removed. Remote "
           "configurations and local clones will not be affected.</p>");

    QMessageBox mb(QMessageBox::Warning, tr("Remove Account?"),
                   fmt.arg(Account::name(kind), name), QMessageBox::Cancel,
                   this);
    QPushButton *remove = mb.addButton(tr("Remove"), QMessageBox::AcceptRole);
    remove->setFocus();

    mb.exec();
    if (mb.clickedButton() == remove) {
      mHostTree->clearSelection();
      Accounts::instance()->removeAccount(index.row());
    }

    return;
  }

  // repo
  Repository *repo = index.data(RepositoryRole).value<Repository *>();
  QString fmt =
      tr("<p>Are you sure you want to remove the remote repository association "
         "for %1?</p><p>The local clone itself will not be affected.</p>");

  QMessageBox mb(QMessageBox::Warning, tr("Remove Repository Association?"),
                 fmt.arg(repo->fullName()), QMessageBox::Cancel, this);
  QPushButton *remove = mb.addButton(tr("Remove"), QMessageBox::AcceptRole);
  remove->setFocus();

  mb.exec();
  if (mb.clickedButton() == remove)
    repo->account()->setRepositoryPath(index.row(), QString());

  // Update minus and OK buttons.
  updateButtons();
}

MainWindow *StartDialog::openWindow(const QString &repo) {
  // This dialog has to be hidden before opening another window so that it
  // doesn't automatically move to a position that still shows the dialog.
  hide();
  if (MainWindow *window = MainWindow::open(repo)) {
    // Dismiss this dialog without trying to open the selection.
    reject();
    return window;
  } else {
    show();
  }

  return nullptr;
}

#include "StartDialog.moc"
