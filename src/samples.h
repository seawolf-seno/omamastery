#pragma once

#include <QString>

// Sets up ~/Classes: the school schedule, the agent guide, and (on request)
// made-up sample classes. Never overwrites a file that is already there.
namespace Samples {

bool init(const QString &root, bool withSampleClasses, QString *report, QString *error);

QString scheduleToml();
QString agentsGuide();
// The rules for real (non-sample) classes, and adding them to an existing
// AGENTS.md that predates them. Never removes anything the teacher wrote.
QString realClassRules();
void addRealClassRules(const QString &agentsPath);

} // namespace Samples
