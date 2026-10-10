#include "FileManagerIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString exe() {
  return QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath();
}

QString dataDir(const QString &relative) {
  QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return QDir(base).filePath(relative);
}

QString configDir(const QString &relative) {
  QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  return QDir(base).filePath(relative);
}

bool writeFile(const QString &path, const QString &contents, bool executable) {
  QDir().mkpath(QFileInfo(path).path());
  QFile file(path);
  if (!file.open(QFile::WriteOnly | QFile::Truncate))
    return false;
  file.write(contents.toUtf8());
  file.close();
  if (executable)
    file.setPermissions(file.permissions() | QFile::ExeOwner | QFile::ExeGroup);
  return true;
}

QString shellQuote(const QString &path) { return "'" + QString(path).replace("'", "'\\''") + "'"; }

const char *kInRepo = R"(#!/bin/sh
target="$1"
[ -n "$target" ] || exit 1
if [ ! -d "$target" ]; then
  target=$(dirname "$target")
fi
git -C "$target" rev-parse --is-inside-work-tree >/dev/null 2>&1
)";

QString nemoAction(const QString &name, const QString &flag, bool fileOnly) {
  QString exec = shellQuote(exe());
  if (!flag.isEmpty())
    exec += " " + flag;
  return QString("[Nemo Action]\n"
                 "Name=%1\n"
                 "Exec=%2 %F\n"
                 "Selection=s\n"
                 "Extensions=%3\n"
                 "Dependencies=git;\n"
                 "Conditions=exec <gittyup-in-repo.sh> %F;\n")
      .arg(name, exec, fileOnly ? "nodirs;" : "any;");
}

QString nautilusScript(const QString &flag) {
  QString command = shellQuote(exe());
  if (!flag.isEmpty())
    command += " " + flag;
  return QString("#!/bin/sh\n"
                 "printf '%s\\n' \"$NAUTILUS_SCRIPT_SELECTED_FILE_PATHS\" | while IFS= read -r path; do\n"
                 "  [ -n \"$path\" ] || continue\n"
                 "  target=\"$path\"\n"
                 "  [ -d \"$target\" ] || target=$(dirname \"$target\")\n"
                 "  git -C \"$target\" rev-parse --is-inside-work-tree >/dev/null 2>&1 || exit 0\n"
                 "  %1 \"$path\"\n"
                 "  break\n"
                 "done\n")
      .arg(command);
}

QString dolphinMenu() {
  QString command = exe();
  return QString("[Desktop Entry]\n"
                 "Type=Service\n"
                 "ServiceTypes=KonqPopupMenu/Plugin\n"
                 "MimeType=all/all;inode/directory;\n"
                 "Actions=GittyupHistory;GittyupBlame;GittyupStatus;GittyupOpen;\n"
                 "X-KDE-Priority=TopLevel\n"
                 "\n"
                 "[Desktop Action GittyupHistory]\n"
                 "Name=Gittyup: History\n"
                 "Exec=%1 --history %f\n"
                 "\n"
                 "[Desktop Action GittyupBlame]\n"
                 "Name=Gittyup: Blame\n"
                 "Exec=%1 --blame %f\n"
                 "\n"
                 "[Desktop Action GittyupStatus]\n"
                 "Name=Gittyup: Changes\n"
                 "Exec=%1 --status %f\n"
                 "\n"
                 "[Desktop Action GittyupOpen]\n"
                 "Name=Gittyup: Open repository\n"
                 "Exec=%1 %f\n")
      .arg(command);
}

QString thunarBlock() {
  QString command = exe();
  return QString("<!-- gittyup begin -->\n"
                 "<action>\n"
                 "<icon>text-x-generic</icon>\n"
                 "<name>Gittyup: History</name>\n"
                 "<unique-id>gittyup-history</unique-id>\n"
                 "<command>%1 --history %f</command>\n"
                 "<patterns>*</patterns>\n"
                 "<directories/>\n"
                 "</action>\n"
                 "<action>\n"
                 "<icon>text-x-generic</icon>\n"
                 "<name>Gittyup: Blame</name>\n"
                 "<unique-id>gittyup-blame</unique-id>\n"
                 "<command>%1 --blame %f</command>\n"
                 "<patterns>*</patterns>\n"
                 "</action>\n"
                 "<action>\n"
                 "<icon>text-x-generic</icon>\n"
                 "<name>Gittyup: Changes</name>\n"
                 "<unique-id>gittyup-status</unique-id>\n"
                 "<command>%1 --status %f</command>\n"
                 "<patterns>*</patterns>\n"
                 "<directories/>\n"
                 "</action>\n"
                 "<!-- gittyup end -->\n")
      .arg(command);
}

void installThunar() {
  QString path = configDir("Thunar/uca.xml");
  QDir().mkpath(QFileInfo(path).path());
  QString existing;
  QFile current(path);
  if (current.open(QFile::ReadOnly))
    existing = QString::fromUtf8(current.readAll());
  int begin = existing.indexOf("<!-- gittyup begin -->");
  int end = existing.indexOf("<!-- gittyup end -->");
  QString block = thunarBlock();
  if (begin >= 0 && end > begin) {
    existing.replace(begin, end + QString("<!-- gittyup end -->").size() - begin, block);
  } else if (existing.contains("</actions>")) {
    existing.replace("</actions>", block + "</actions>");
  } else {
    existing = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<actions>\n" + block +
               "</actions>\n";
  }
  writeFile(path, existing, false);
}

void uninstallThunar() {
  QString path = configDir("Thunar/uca.xml");
  QFile current(path);
  if (!current.open(QFile::ReadOnly))
    return;
  QString existing = QString::fromUtf8(current.readAll());
  int begin = existing.indexOf("<!-- gittyup begin -->");
  int end = existing.indexOf("<!-- gittyup end -->");
  if (begin < 0 || end < begin)
    return;
  existing.remove(begin, end + QString("<!-- gittyup end -->").size() - begin);
  writeFile(path, existing, false);
}

} // namespace

QStringList FileManagerIntegration::installedFileManagers() {
  QStringList found;
  const QStringList names = {"nemo", "nautilus", "dolphin", "thunar", "caja"};
  for (const QString &name : names) {
    if (!QStandardPaths::findExecutable(name).isEmpty())
      found.append(name);
  }
  return found;
}

bool FileManagerIntegration::install() {
  bool ok = true;
  QString nemo = dataDir("nemo/actions");
  ok &= writeFile(nemo + "/gittyup-in-repo.sh", kInRepo, true);
  ok &= writeFile(nemo + "/gittyup-history.nemo_action",
                  nemoAction("Gittyup: History", "--history", false), false);
  ok &= writeFile(nemo + "/gittyup-blame.nemo_action",
                  nemoAction("Gittyup: Blame", "--blame", true), false);
  ok &= writeFile(nemo + "/gittyup-status.nemo_action",
                  nemoAction("Gittyup: Changes", "--status", false), false);
  ok &= writeFile(nemo + "/gittyup-open.nemo_action",
                  nemoAction("Gittyup: Open repository", QString(), false), false);

  auto scripts = [&](const QString &root) {
    ok &= writeFile(root + "/Gittyup History", nautilusScript("--history"), true);
    ok &= writeFile(root + "/Gittyup Blame", nautilusScript("--blame"), true);
    ok &= writeFile(root + "/Gittyup Changes", nautilusScript("--status"), true);
    ok &= writeFile(root + "/Gittyup Open", nautilusScript(QString()), true);
  };
  scripts(dataDir("nautilus/scripts"));
  scripts(configDir("caja/scripts"));

  ok &= writeFile(dataDir("kio/servicemenus/gittyup.desktop"), dolphinMenu(), false);
  installThunar();
  return ok;
}

bool FileManagerIntegration::uninstall() {
  const QStringList files = {
      dataDir("nemo/actions/gittyup-in-repo.sh"),
      dataDir("nemo/actions/gittyup-history.nemo_action"),
      dataDir("nemo/actions/gittyup-blame.nemo_action"),
      dataDir("nemo/actions/gittyup-status.nemo_action"),
      dataDir("nemo/actions/gittyup-open.nemo_action"),
      dataDir("nautilus/scripts/Gittyup History"),
      dataDir("nautilus/scripts/Gittyup Blame"),
      dataDir("nautilus/scripts/Gittyup Changes"),
      dataDir("nautilus/scripts/Gittyup Open"),
      configDir("caja/scripts/Gittyup History"),
      configDir("caja/scripts/Gittyup Blame"),
      configDir("caja/scripts/Gittyup Changes"),
      configDir("caja/scripts/Gittyup Open"),
      dataDir("kio/servicemenus/gittyup.desktop"),
  };
  for (const QString &path : files)
    QFile::remove(path);
  uninstallThunar();
  return true;
}
