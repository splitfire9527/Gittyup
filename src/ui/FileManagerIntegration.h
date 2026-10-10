#ifndef FILEMANAGERINTEGRATION_H
#define FILEMANAGERINTEGRATION_H

#include <QStringList>

// Installs context-menu entries for Nemo, Nautilus, Dolphin, Thunar and Caja.
// Entries call this program with --history, --blame, --status or a path.
class FileManagerIntegration {
public:
  static QStringList installedFileManagers();
  static bool install();
  static bool uninstall();
};

#endif
