#pragma once

#include <QStringList>

namespace SetupCli {

// omamastery setup [--canvas URL] [--blackbaud] [--yes] [--dry-run] [--forget]
int run(const QStringList &arguments);
// omamastery draft: opens the agent to draft objectives for the real classes.
int draft();

} // namespace SetupCli
