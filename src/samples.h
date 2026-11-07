#pragma once

#include <QString>

// Sets up ~/Classes: the school schedule, the agent guide, and (on request)
// made-up sample classes. Never overwrites a file that is already there.
namespace Samples {

bool init(const QString &root, bool withSampleClasses, QString *report, QString *error);

QString scheduleToml();
QString agentsGuide();

} // namespace Samples
