#pragma once

#include <QString>
#include <QStringList>

// Word documents a teacher drops into a class folder (long-range plans,
// syllabi) get a Markdown copy beside them, so any agent can read them.
// Uses LibreOffice when it's installed; without it, nothing happens.
namespace Documents {

// The Markdown name for a .docx: long-range-plan.docx → long-range-plan.md,
// or syllabus.docx → syllabus-document.md when syllabus.md is something else
// (the Canvas syllabus).
QString markdownNameFor(const QString &docxPath);

// Converts each .docx in dir that has no up-to-date Markdown copy. Returns the
// names of the copies it wrote. Never overwrites a file it didn't make.
QStringList convertForAgents(const QString &dir);

} // namespace Documents
