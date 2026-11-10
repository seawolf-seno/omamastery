// Cleaning up classes from a terminal: archive (end of year), delete (to the
// Trash), restore (from the archive).

#include "manageclasses.h"

#include <QDir>
#include <QTextStream>

#include <unistd.h>

#include "classbook.h"

namespace {

QTextStream &out() {
    static QTextStream s(stdout);
    return s;
}

} // namespace

int ManageClasses::archive(const QStringList &arguments) {
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));
    QStringList ids;
    if (arguments.contains("--all")) {
        for (const auto &info : book.loadClasses())
            ids << info.id;
    } else {
        for (const QString &a : arguments.mid(2))
            if (!a.startsWith("--"))
                ids << a;
    }
    if (ids.isEmpty()) {
        out() << "Usage: omamastery archive CLASS... | --all\n"
                 "Moves classes to " << book.root() << "/archive/" << book.schoolYear() << "/, with every file.\n";
        return 1;
    }
    int failed = 0;
    for (const QString &id : ids) {
        QString movedTo, error;
        if (book.archiveClass(id, &movedTo, &error))
            out() << "Archived " << id << " in " << QDir::home().relativeFilePath(movedTo).prepend("~/") << "\n";
        else {
            out() << error << "\n";
            ++failed;
        }
    }
    return failed ? 1 : 0;
}

int ManageClasses::remove(const QStringList &arguments) {
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));
    const QString id = arguments.value(2);
    if (id.isEmpty() || id.startsWith("--")) {
        out() << "Usage: omamastery delete CLASS [--yes]\nMoves the class's folder to the Trash.\n";
        return 1;
    }
    if (!arguments.contains("--yes")) {
        if (!isatty(STDIN_FILENO)) {
            out() << "Add --yes to move " << id << " to the Trash.\n";
            return 1;
        }
        out() << "Move " << id << " (roster, plan and mastery levels) to the Trash? [y/N] " << Qt::flush;
        const QString answer = QTextStream(stdin).readLine().trimmed().toLower();
        if (answer != "y" && answer != "yes")
            return 1;
    }
    QString error;
    if (!book.deleteClass(id, &error)) {
        out() << error << "\n";
        return 1;
    }
    out() << "Moved " << id << " to the Trash.\n";
    return 0;
}

int ManageClasses::restore(const QStringList &arguments) {
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));
    const QString path = arguments.value(2);
    if (path.isEmpty()) {
        out() << "Usage: omamastery restore " << book.root() << "/archive/<year>/<class>\n";
        return 1;
    }
    QString error;
    if (!book.restoreClass(QDir::current().absoluteFilePath(path), &error)) {
        out() << error << "\n";
        return 1;
    }
    out() << "Restored " << QDir(path).dirName() << ".\n";
    return 0;
}
