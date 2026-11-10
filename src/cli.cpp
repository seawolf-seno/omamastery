#include "cli.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "classbook.h"
#include "samples.h"
#include "school.h"
#include "manageclasses.h"
#include "setupcli.h"

namespace {

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err() {
    static QTextStream stream(stderr);
    return stream;
}

QJsonObject classJson(const ClassInfo &info, const NowInfo &now, int index) {
    return QJsonObject{{"id", info.id},
                       {"name", info.name},
                       {"day", info.day},
                       {"block", info.block},
                       {"room", info.room},
                       {"sample", info.sample},
                       {"dir", info.dir},
                       {"students", QJsonArray::fromStringList(info.students)},
                       {"now", index == now.index && now.status == NowInfo::Now},
                       {"next", index == now.index && now.status == NowInfo::Next}};
}

int usage() {
    err() << "Usage: omamastery [open [CLASS] | classes [--json] | now [--json] | init [--sample]\n"
             "                   | setup [--canvas URL] [--blackbaud] [--yes] [--dry-run] [--forget] | draft\n"
             "                   | archive CLASS... | archive --all | delete CLASS [--yes] | restore PATH]\n";
    return 1;
}

} // namespace

bool Cli::handles(const QStringList &arguments) {
    if (arguments.size() < 2)
        return false;
    static const QStringList commands{"classes", "now", "init", "setup", "draft", "archive", "delete", "restore", "help", "--help", "-h"};
    return commands.contains(arguments[1]);
}

int Cli::run(const QStringList &arguments) {
    const QString command = arguments.value(1);
    const bool json = arguments.contains("--json");
    Classbook book(qEnvironmentVariable("OMAMASTERY_HOME"));

    if (command == "setup")
        return SetupCli::run(arguments);
    if (command == "draft")
        return SetupCli::draft();
    if (command == "archive")
        return ManageClasses::archive(arguments);
    if (command == "delete")
        return ManageClasses::remove(arguments);
    if (command == "restore")
        return ManageClasses::restore(arguments);
    if (command == "init") {
        QString report;
        QString error;
        if (!Samples::init(book.root(), arguments.contains("--sample"), &report, &error)) {
            err() << error << "\n";
            return 1;
        }
        out() << report << "\n";
        return 0;
    }
    if (command == "classes" || command == "now") {
        if (!book.exists()) {
            err() << "No classes yet: run `omamastery init --sample` to set up " << book.root() << "\n";
            return 1;
        }
        Schedule schedule;
        QString error;
        if (!schedule.load(book.schedulePath(), &error)) {
            err() << error << "\n";
            return 1;
        }
        const auto classes = book.loadClasses();
        const NowInfo now = Classbook::now(classes, schedule, QDateTime::currentDateTime());

        if (command == "now") {
            if (json) {
                QJsonObject object{{"status", now.status == NowInfo::Now ? "now" : now.status == NowInfo::Next ? "next" : "none"},
                                   {"text", now.text}};
                if (now.index >= 0)
                    object.insert("class", classJson(classes[now.index], now, now.index));
                out() << QJsonDocument(object).toJson(QJsonDocument::Compact) << "\n";
            } else {
                out() << (now.index >= 0 ? classes[now.index].name + " · " : QString()) << now.text << "\n";
            }
            return 0;
        }

        if (json) {
            QJsonArray list;
            for (int i = 0; i < classes.size(); ++i)
                list.append(classJson(classes[i], now, i));
            out() << QJsonDocument(QJsonObject{{"classes", list}, {"now", now.text}}).toJson(QJsonDocument::Compact) << "\n";
        } else {
            for (int i = 0; i < classes.size(); ++i) {
                const auto &info = classes[i];
                out() << (i == now.index ? "> " : "  ") << info.day << info.block << "  " << info.id << "  " << info.name
                      << (info.sample ? "  (sample)" : "") << "\n";
            }
        }
        return 0;
    }
    return usage();
}
