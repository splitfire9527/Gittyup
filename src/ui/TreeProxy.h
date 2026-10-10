//
//
// This software is licensed under the MIT License. The LICENSE.md file
// describes the conditions under which this software may be distributed.
//
// Author: Martin Marmsoler
//

#ifndef TREEPROXY_H
#define TREEPROXY_H

#include "git/Diff.h"
#include "git/Index.h"
#include "git/Tree.h"
#include "git/Repository.h"
#include <QSet>
#include <QSortFilterProxyModel>
#include <QFileIconProvider>

class QAbstractItemModel;
class TreeModel;

class TreeProxy : public QSortFilterProxyModel {
  Q_OBJECT

public:
  TreeProxy(bool staged, QAbstractItemModel *model, QObject *parent);
  virtual ~TreeProxy();
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole, bool ignoreIndexChanges = false);
  bool staged() { return mStaged; }

  void enableFilter(bool enable) { mFilter = enable; }

  enum class EolFilter { All, Only, Exclude };
  void setEolFilter(EolFilter filter, const QSet<QString> &paths);

  int columnCount(const QModelIndex &parent = QModelIndex()) const override {
    return sourceModel()->columnCount();
  }

private:
  using QSortFilterProxyModel::setData;
  bool filterAcceptsRow(int source_row,
                        const QModelIndex &source_parent) const override;
  bool matchesEol(const QModelIndex &index) const;
  bool isOnlyEol(const QModelIndex &index) const;
  bool mStaged{
      true}; // indicates, if only staged or only unstages files should be shown
  bool mFilter = true;
  EolFilter mEolFilter = EolFilter::All;
  QSet<QString> mEolOnly;
};

#endif // TREEPROXY_H
