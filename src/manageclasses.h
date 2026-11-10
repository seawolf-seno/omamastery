#pragma once

#include <QStringList>

// omamastery archive [--all | CLASS...], delete CLASS [--yes], restore PATH
namespace ManageClasses {
int archive(const QStringList &arguments);
int remove(const QStringList &arguments);
int restore(const QStringList &arguments);
} // namespace ManageClasses
