//
//          Copyright (c) 2016, Scientific Toolworks, Inc.
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Jason Haslam
//

#include "TreeProxy.h"
#include "DiffTreeModel.h"
#include "conf/Settings.h"
#include "git/Blob.h"
#include "git/Diff.h"
#include "git/RevWalk.h"
#include "git/Submodule.h"
#include <QStringBuilder>
#include <QUrl>

namespace {

const QString kLinkFmt = "<a href='%1'>%2</a>";

} // namespace

TreeProxy::TreeProxy(bool staged, QAbstractItemModel *model, QObject *parent)
    : QSortFilterProxyModel(parent), mStaged(staged) {
  setSourceModel(model);
}

TreeProxy::~TreeProxy() {}

bool TreeProxy::setData(const QModelIndex &index, const QVariant &value,
                        int role, bool ignoreIndexChanges) {
  QModelIndex sourceIndex = mapToSource(index);
  if (index.isValid() && !sourceIndex.isValid())
    return false;

  return static_cast<DiffTreeModel *>(sourceModel())
      ->setData(sourceIndex, value, role, ignoreIndexChanges);
}

void TreeProxy::setEolFilter(EolFilter filter, const QSet<QString> &paths) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
  beginFilterChange();
  mEolFilter = filter;
  mEolOnly = paths;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
  mEolFilter = filter;
  mEolOnly = paths;
  invalidateFilter();
#endif
}

bool TreeProxy::matchesEol(const QModelIndex &index) const {
  QString path = sourceModel()->data(index, Qt::EditRole).toString();
  if (mEolOnly.contains(path))
    return true;
  int rows = sourceModel()->rowCount(index);
  for (int i = 0; i < rows; ++i) {
    if (matchesEol(sourceModel()->index(i, 0, index)))
      return true;
  }
  return false;
}

bool TreeProxy::isOnlyEol(const QModelIndex &index) const {
  int rows = sourceModel()->rowCount(index);
  if (rows == 0) {
    return mEolOnly.contains(
        sourceModel()->data(index, Qt::EditRole).toString());
  }
  for (int i = 0; i < rows; ++i) {
    if (!isOnlyEol(sourceModel()->index(i, 0, index)))
      return false;
  }
  return true;
}

bool TreeProxy::filterAcceptsRow(int source_row,
                                 const QModelIndex &source_parent) const {
  QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
  if (!index.isValid())
    return false;

  if (!mFilter)
    return true;

  // This is anymore needed, because only the diff tree is checked and so every
  // file is modified or was added
  //    QString status = sourceModel()->data(index,
  //    DiffTreeModel::StatusRole).toString();
  //	QRegExp regexp(".*[AM?].*");
  //	if (!status.contains(regexp))
  //		return false; // if the file/folder was not modified/added ...
  // don't show it in the tree view

  Qt::CheckState state = static_cast<Qt::CheckState>(
      sourceModel()->data(index, Qt::CheckStateRole).toInt());
  if (mStaged && state == Qt::CheckState::Unchecked)
    return false;
  else if (!mStaged && state == Qt::CheckState::Checked)
    return false;

  if (mStaged || mEolFilter == EolFilter::All)
    return true;
  if (mEolFilter == EolFilter::Only)
    return matchesEol(index);
  return !isOnlyEol(index);
}
