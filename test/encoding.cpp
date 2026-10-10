#include "Test.h"
#include "git/Index.h"
#include <QDir>
#include <QFile>
#include <QSet>

using namespace Test;

class TestEncoding : public QObject {
  Q_OBJECT

private slots:
  void decodePrefersUtf8();
  void decodeGbkAfterUtf8Fails();
  void eolOnlyPaths();
};

void TestEncoding::decodePrefersUtf8() {
  ScratchRepository scratch;
  git::Repository repo = scratch;
  QByteArray utf8 = QStringLiteral("中文").toUtf8();
  QCOMPARE(repo.decode(utf8, "notes.txt"), QStringLiteral("中文"));
}

void TestEncoding::decodeGbkAfterUtf8Fails() {
  ScratchRepository scratch;
  git::Repository repo = scratch;
  // GBK bytes for 中文. These are not valid UTF-8.
  QByteArray gbk = QByteArray::fromHex("D6D0CEC4");
  QCOMPARE(repo.decode(gbk, "notes.txt"), QStringLiteral("中文"));
}

void TestEncoding::eolOnlyPaths() {
  ScratchRepository scratch;
  git::Repository repo = scratch;
  QString workdir = repo.workdir().path();
  QVERIFY(forceAdd(workdir, "eol.txt", "one\n"));
  QVERIFY(forceAdd(workdir, "content.txt", "a\n"));
  QVERIFY(repo.commit("init").isValid());

  QFile eol(QDir(workdir).filePath("eol.txt"));
  QVERIFY(eol.open(QFile::WriteOnly | QFile::Truncate));
  eol.write("one\r\n");
  eol.close();

  QFile content(QDir(workdir).filePath("content.txt"));
  QVERIFY(content.open(QFile::WriteOnly | QFile::Truncate));
  content.write("b\n");
  content.close();

  QFile extra(QDir(workdir).filePath("extra.txt"));
  QVERIFY(extra.open(QFile::WriteOnly));
  extra.write("x\r\n");
  extra.close();

  QSet<QString> paths = repo.eolOnlyPaths();
  QVERIFY(paths.contains("eol.txt"));
  QVERIFY(!paths.contains("content.txt"));
  QVERIFY(!paths.contains("extra.txt"));
}

TEST_MAIN(TestEncoding)

#include "encoding.moc"
