#pragma once

#include <QStringList>

// Commands that need no window, for agents, scripts and OmaClass:
//   omamastery classes [--json]    every class, with the one happening now
//   omamastery now [--json]        the class happening now, or next today
//   omamastery init [--sample]     set up ~/Classes (schedule, agent guide, sample classes)
// Each exits 0 on success and 1 on failure, with errors on stderr.
namespace Cli {

bool handles(const QStringList &arguments);
int run(const QStringList &arguments);

} // namespace Cli
