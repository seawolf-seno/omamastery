#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "classbook.h"
#include "connect/connect.h"
#include "connect/importer.h"
#include "samples.h"
#include "school.h"
#include "theme.h"
#include "translator.h"

class OmaMasteryTests : public QObject {
    Q_OBJECT

private:
    static void write(const QString &path, const QByteArray &contents) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }

    static Schedule sampleSchedule(QTemporaryDir &dir) {
        write(dir.path() + "/schedule.toml", Samples::scheduleToml().toUtf8());
        Schedule schedule;
        schedule.load(dir.path() + "/schedule.toml");
        return schedule;
    }

private slots:
    void scheduleKnowsAandBDays() {
        QTemporaryDir dir;
        const Schedule schedule = sampleSchedule(dir);
        QCOMPARE(schedule.day(QDate(2026, 11, 2)).type, QString("A"));  // Monday
        QCOMPARE(schedule.day(QDate(2026, 11, 3)).type, QString("B"));  // Tuesday
        QCOMPARE(schedule.day(QDate(2026, 11, 6)).type, QString("A"));  // listed Friday
        QCOMPARE(schedule.day(QDate(2026, 11, 20)).type, QString("B")); // listed Friday
        QCOMPARE(schedule.day(QDate(2026, 11, 7)).kind, DayInfo::Weekend);
        QCOMPARE(schedule.day(QDate(2026, 11, 11)).kind, DayInfo::Off);
        QCOMPARE(schedule.day(QDate(2026, 11, 25)).label, QString("Thanksgiving Break"));
        QCOMPARE(schedule.day(QDate(2027, 7, 1)).kind, DayInfo::OutOfYear);
    }

    void scheduleFindsBlocks() {
        QTemporaryDir dir;
        const Schedule schedule = sampleSchedule(dir);
        QCOMPARE(schedule.blockAt(QTime(8, 30)), 1);
        QCOMPARE(schedule.blockAt(QTime(9, 30)), 0);
        QCOMPARE(schedule.nextBlockAfter(QTime(9, 30)), 2);
        QCOMPARE(schedule.nextBlockAfter(QTime(15, 0)), 0);
    }

    void badScheduleReportsTheLine() {
        QTemporaryDir dir;
        write(dir.path() + "/schedule.toml", "first_day = \n");
        Schedule schedule;
        QString error;
        QVERIFY(!schedule.load(dir.path() + "/schedule.toml", &error));
        QVERIFY(error.contains("schedule.toml"));
    }

    void nowFindsTheClassInTheCurrentBlock() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        Classbook book(dir.path());
        Schedule schedule;
        QVERIFY(schedule.load(book.schedulePath()));
        const auto classes = book.loadClasses();
        QCOMPARE(classes.size(), 3);

        // Monday 8:30: A day, block 1 is AP CSP.
        NowInfo now = Classbook::now(classes, schedule, QDateTime(QDate(2026, 11, 2), QTime(8, 30)));
        QCOMPARE(now.status, NowInfo::Now);
        QCOMPARE(classes[now.index].id, QString("ap-csp"));

        // Monday 9:30: between blocks, block 2 is free, so next is block 3.
        now = Classbook::now(classes, schedule, QDateTime(QDate(2026, 11, 2), QTime(9, 30)));
        QCOMPARE(now.status, NowInfo::Next);
        QCOMPARE(classes[now.index].id, QString("advanced-cs"));

        // Tuesday 10:00: B day, block 2 is Foundations.
        now = Classbook::now(classes, schedule, QDateTime(QDate(2026, 11, 3), QTime(10, 0)));
        QCOMPARE(classes[now.index].id, QString("foundations"));

        // Veterans Day: no class, and the reason.
        now = Classbook::now(classes, schedule, QDateTime(QDate(2026, 11, 11), QTime(10, 0)));
        QCOMPARE(now.status, NowInfo::None);
        QVERIFY(now.text.contains("Veterans"));
    }

    void masteryLogKeepsTheLatestLevel() {
        QTemporaryDir dir;
        const QString csv = dir.path() + "/mastery.csv";
        QVERIFY(Classbook::appendMastery(csv, QDate(2026, 11, 2), "Ava", "loops", "Developing", "Quiz"));
        QVERIFY(Classbook::appendMastery(csv, QDate(2026, 11, 4), "Ava", "loops", "Proficient", "Oral, with \"quotes\", and commas"));
        QVERIFY(!Classbook::appendMastery(csv, QDate(2026, 11, 4), "Ava", "loops", "A+", ""));
        const auto levels = Classbook::loadMastery(csv);
        QCOMPARE(levels.value(Classbook::masteryKey("Ava", "loops")), QString("Proficient"));
        QCOMPARE(Classbook::parseCsv(QString::fromUtf8([&] { QFile f(csv); f.open(QIODevice::ReadOnly); return f.readAll(); }())).size(), 3);
    }

    void csvRoundTripsAwkwardText() {
        const QString field = "He said \"hi\", then left";
        const auto rows = Classbook::parseCsv(Classbook::csvField(field) + ",b\n");
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows[0][0], field);
        QCOMPARE(rows[0][1], QString("b"));
    }

    void initNeverOverwrites() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        write(dir.path() + "/ap-csp/notes.md", "mine");
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        QFile notes(dir.path() + "/ap-csp/notes.md");
        QVERIFY(notes.open(QIODevice::ReadOnly));
        QCOMPARE(notes.readAll(), QByteArray("mine"));
    }

    void backendOpensTheClassHappeningNowAndSavesLevels() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        Backend backend(dir.path());
        backend.setClock(QDateTime(QDate(2026, 11, 3), QTime(10, 0)));
        QCOMPARE(backend.current().value("id").toString(), QString("foundations"));

        backend.selectClassId("ap-csp");
        backend.cycleLevel("Ava", "csp-search", 1);
        QCOMPARE(backend.levels().value("Ava|csp-search").toString(), QString("Beginning"));
        backend.cycleLevel("Ava", "csp-search", 1);
        QCOMPARE(Classbook::loadMastery(dir.path() + "/ap-csp/mastery.csv").value(Classbook::masteryKey("Ava", "csp-search")),
                 QString("Developing"));
    }

    void backendSeesAnAgentsEdit() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        Backend backend(dir.path());
        backend.selectClassId("ap-csp");
        QSignalSpy spy(&backend, &Backend::changed);
        write(dir.path() + "/ap-csp/notes.md", "## Today\n- Reteach filtering\n");
        QTRY_VERIFY(backend.notes().contains("Reteach filtering"));
    }

    void agentPromptsPointAtTheRules() {
        ClassInfo info;
        info.name = "AP CSP";
        for (const char *kind : {"next", "space", "objectives"}) {
            const QString prompt = Backend::agentPrompt(kind, info);
            QVERIFY(prompt.contains("AGENTS.md"));
            QVERIFY(prompt.contains("AP CSP"));
        }
        // A real class: every prompt fences off the student files and none asks for them.
        info.sample = false;
        for (const char *kind : {"next", "space", "objectives"}) {
            const QString prompt = Backend::agentPrompt(kind, info);
            QVERIFY(prompt.contains("never open roster.toml, mastery.csv or notes.md"));
            QCOMPARE(prompt.count("mastery.csv"), 1);
            QCOMPARE(prompt.count("notes.md"), 1);
        }
        QVERIFY(Connect::draftPrompt({"robotics"}).contains("Never open roster.toml or mastery.csv"));
    }

    void setupReadsDayAndBlockFromNames() {
        struct Case { const char *text; const char *day; int block; };
        const Case cases[] = {{"AP CSP - Block 3A", "A", 3}, {"Physics 2B", "B", 2}, {"Robotics A-4", "A", 4},
                              {"Period 1", "", 1}, {"Chemistry (B)", "B", 0}, {"Advisory", "", 0},
                              {"Algebra 2", "", 0}};
        for (const auto &c : cases) {
            QString day;
            int block = 0;
            Importer::guessDayBlock(c.text, &day, &block);
            QCOMPARE(day, QString(c.day));
            QCOMPARE(block, c.block);
        }
    }

    void setupReadsBlackbaudClockTimes() {
        QCOMPARE(Importer::clockTime("1900-01-01T08:05:00-05:00"), QTime(8, 5));
        QCOMPARE(Importer::clockTime("13:15"), QTime(13, 15));
        QCOMPARE(Importer::clockTime("1:15 PM"), QTime(13, 15));
        QCOMPARE(Importer::clockTime("12:10 AM"), QTime(0, 10));
        QVERIFY(!Importer::clockTime("soon").isValid());
    }

    void setupNormalizesCanvasAddresses() {
        using S = Connect::Settings;
        QCOMPARE(S::normalizeCanvasUrl("pacificachristian"), QString("https://pacificachristian.instructure.com"));
        QCOMPARE(S::normalizeCanvasUrl("school.instructure.com/courses/12/"), QString("https://school.instructure.com"));
        QCOMPARE(S::normalizeCanvasUrl("http://canvas.school.org"), QString("https://canvas.school.org"));
        QCOMPARE(S::normalizeCanvasUrl("http://127.0.0.1:8080"), QString("http://127.0.0.1:8080"));
        QCOMPARE(S::normalizeCanvasUrl("  "), QString());
    }

    void setupTellsStudentsWithOneNameApart() {
        const QJsonArray users = QJsonDocument::fromJson(R"([
            {"id": 1, "short_name": "Sam", "sortable_name": "Arden, Sam"},
            {"id": 2, "short_name": "Sam", "sortable_name": "Berry, Sam"},
            {"id": 3, "short_name": "Lio", "sortable_name": "Park, Lio"},
            {"id": 3, "short_name": "Lio", "sortable_name": "Park, Lio"},
            {"id": 4, "short_name": "Ana", "sortable_name": "Cole, Ana"},
            {"id": 5, "short_name": "Ana", "sortable_name": "Cruz, Ana"}])").array();
        QStringList names;
        for (const auto &s : Importer::studentsFromCanvas(users))
            names << s.name;
        QCOMPARE(names, QStringList({"Sam A.", "Sam B.", "Lio", "Ana Cole", "Ana Cruz"}));
    }

    void setupMakesShortFolderNames() {
        QCOMPARE(Importer::slug("AP Computer Science Principles - Block 1A"), QString("ap-computer-science-principles-block-1a"));
        QCOMPARE(Importer::slug("  Español: Nivel II! "), QString("espa-ol-nivel-ii"));
        QVERIFY(Importer::slug("A very long course name that keeps going and going forever").size() <= 40);
    }

    void classbookReadsTheRoster() {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("c"));
        write(dir.filePath("c/class.toml"), "name = \"Real\"\nsample = false\n");
        write(dir.filePath("c/roster.toml"), "[[student]]\nname = \"Mara\"\ncanvas_id = \"1\"\n\n[[student]]\nname = \"Teo\"\n");
        const ClassInfo info = Classbook::loadClass(dir.filePath("c"));
        QCOMPARE(info.students, QStringList({"Mara", "Teo"}));
        QVERIFY(!info.sample);
    }

    void agentGuideFencesOffRealStudents() {
        QVERIFY(Samples::agentsGuide().contains("Never open `roster.toml`, `mastery.csv` or `notes.md`"));
        QTemporaryDir dir;
        const QString path = dir.filePath("AGENTS.md");
        write(path, "# My own guide\n");
        Samples::addRealClassRules(path);
        Samples::addRealClassRules(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY(text.startsWith("# My own guide"));
        QCOMPARE(text.count("## Real classes"), 1);
    }

    void themeFallsBack() {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
    }

    void translatorFallsBackToEnglish() {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
    }
};

QTEST_MAIN(OmaMasteryTests)
#include "omamastery_tests.moc"
