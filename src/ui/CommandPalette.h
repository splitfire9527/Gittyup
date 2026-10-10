#ifndef COMMANDPALETTE_H
#define COMMANDPALETTE_H

#include <QDialog>
#include <QKeyEvent>
#include <functional>

class QLineEdit;
class QListWidget;

class CommandPalette : public QDialog {
  Q_OBJECT

public:
  static void open(QWidget *parent = nullptr);

private:
  explicit CommandPalette(QWidget *parent = nullptr);
  void rebuild(const QString &query);
  void activateCurrent();

protected:
  void keyPressEvent(QKeyEvent *event) override;

  struct Item {
    QString title;
    QString detail;
    std::function<void()> action;
  };

  QLineEdit *mQuery;
  QListWidget *mList;
  QList<Item> mItems;
};

#endif
