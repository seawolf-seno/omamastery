#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "classbook.h"
#include "documents.h"
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

    void fillingAnObjectiveOnlyTouchesEmptyCells() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        Backend backend(dir.path());
        backend.setClock(QDateTime(QDate(2026, 11, 9), QTime(8, 30)));
        backend.reload();
        backend.selectClassId("ap-csp");
        const QString objective = "csp-variables";
        const QString csv = dir.path() + "/ap-csp/mastery.csv";
        QFile::setPermissions(csv, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QStringList students = backend.students();
        QVERIFY(students.size() > 2);
        // Empty two cells by starting from a log without them.
        QVERIFY(QFile::remove(csv));
        QVERIFY(Classbook::appendMastery(csv, QDate(2026, 9, 1), students[0], objective, "Extending", "From Canvas: 10 of 10"));
        QFile::setPermissions(csv, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        backend.reload();
        QCOMPARE(backend.fillObjective(objective, "Proficient"), int(students.size()) - 1);
        QCOMPARE(backend.levels().value(students[0] + "|" + objective).toString(), QString("Extending"));
        QCOMPARE(backend.levels().value(students[1] + "|" + objective).toString(), QString("Proficient"));
        QCOMPARE(backend.fillObjective(objective, "Beginning"), 0);
        backend.reload();
        QCOMPARE(backend.levelNotes().value(students[0] + "|" + objective).toString(), QString("From Canvas: 10 of 10 · Sep 1"));
        QVERIFY(backend.levelNotes().value(students[1] + "|" + objective).toString().startsWith("Set for the whole objective"));
        // Saving from the app keeps a private log private.
        QCOMPARE(QFile(csv).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions());
    }

    void classesArchiveDeleteAndRestore() {
        QTemporaryDir dir;
        QString report, error, movedTo;
        QVERIFY(Samples::init(dir.path(), true, &report, &error));
        // A scratch Trash: never the teacher's own. (Qt uses the home Trash for
        // files on the home disk, so the scratch folder plays home too.)
        const QByteArray realHome = qgetenv("HOME");
        qputenv("HOME", dir.path().toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        QVERIFY(QDir().mkpath(dir.filePath("data")));
        Classbook book(dir.path());
        QCOMPARE(book.schoolYear(), QString("2026-27"));
        const int before = book.loadClasses().size();

        QVERIFY2(book.archiveClass("ap-csp", &movedTo, &error), qPrintable(error));
        QCOMPARE(movedTo, dir.path() + "/archive/2026-27/ap-csp");
        QVERIFY(QFileInfo::exists(movedTo + "/mastery.csv"));
        QVERIFY(QFileInfo::exists(movedTo + "/long-range-plan.md"));
        QCOMPARE(book.loadClasses().size(), before - 1); // gone from the list

        QVERIFY2(book.restoreClass(movedTo, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(dir.path() + "/ap-csp/class.toml"));
        QCOMPARE(book.loadClasses().size(), before);

        QVERIFY2(book.deleteClass("foundations", &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir.path() + "/foundations"));
        QVERIFY(QFileInfo::exists(dir.filePath("data/Trash/files/foundations/class.toml")));
        QCOMPARE(book.loadClasses().size(), before - 1);

        // Only class folders, by name.
        for (const char *bad : {"", "..", "../x", "archive", "nope", "schedule.toml"})
            QVERIFY2(!book.deleteClass(bad, &error) && !book.archiveClass(bad, &movedTo, &error), bad);
        QVERIFY(QFileInfo::exists(dir.path() + "/schedule.toml"));
        QVERIFY(!book.restoreClass(dir.path() + "/advanced-cs", &error)); // not in the archive
        qunsetenv("XDG_DATA_HOME");
        qputenv("HOME", realHome);
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

    void setupSkipsTheNonMasteryGroup() {
        for (const char *name : {"Non-Mastery Assignments", "non-mastery assignments", "Non Mastery", " NonMastery work"})
            QVERIFY2(Importer::isNonMasteryGroup(name), name);
        for (const char *name : {"Mastery Checks", "Assignments", "Labs (non-mastery)", "Nonmasterywork"})
            QVERIFY2(!Importer::isNonMasteryGroup(name), name);
    }

    void canvasScoresBecomeStartingLevels() {
        const Importer::ScoreLevels levels; // the defaults: 10/9/8/7 of 10
        auto level = [&](double score, double points = 10, const QString &grade = {}) {
            Importer::Score s;
            s.score = score;
            s.grade = grade;
            return Importer::levelForScore(s, points, levels);
        };
        QCOMPARE(level(10), QString("Extending"));
        QCOMPARE(level(9.99), QString("Proficient"));
        QCOMPARE(level(9), QString("Proficient"));
        QCOMPARE(level(8), QString("Developing"));
        QCOMPARE(level(7), QString("Beginning"));
        QCOMPARE(level(6.9), QString("Not yet"));
        QCOMPARE(level(0), QString("Not yet"));
        QCOMPARE(level(3, 4, "proficient"), QString("Proficient")); // Canvas's own word wins
        QCOMPARE(level(-1, 10, "85%"), QString("Developing"));
        QCOMPARE(level(-1, 10), QString()); // ungraded: no starting level
        QTemporaryDir dir;
        write(dir.filePath("schedule.toml"), "[score_levels]\nExtending = 95\nProficient = 85\n");
        const auto custom = Importer::loadScoreLevels(dir.filePath("schedule.toml"));
        QCOMPARE(custom.extending, 95.0);
        QCOMPARE(custom.developing, 80.0);
    }

    void startingLevelsFindPlansWithoutCanvasIds() {
        // A plan from 0.2.0: assignment objectives, but no canvas_assignment lines.
        QTemporaryDir dir;
        write(dir.filePath("plan.toml"), "[[objective]]\nid = \"bits\"\ntitle = \"Bits and Binary\"\n\n"
                                         "[[objective]]\nid = \"mine\"\ntitle = \"My own objective\"\n");
        write(dir.filePath("roster.toml"), "[[student]]\nname = \"Mara\"\ncanvas_id = \"1\"\n");
        write(dir.filePath("mastery.csv"), "date,student,objective,level,evidence\n");
        Importer::ClassDraft draft;
        draft.objectives = {{"bits-and-binary", "Bits and Binary", "", QDate(), "31", 10, ""}};
        Importer::Score score;
        score.canvasUser = "1";
        score.assignment = "31";
        score.score = 9;
        draft.scores = {score};
        QString error;
        QCOMPARE(Importer::seedLevels(dir.path(), draft, {}, &error), 1);
        QCOMPARE(Classbook::loadMastery(dir.filePath("mastery.csv")).value(Classbook::masteryKey("Mara", "bits")), QString("Proficient"));
        QCOMPARE(Importer::seedLevels(dir.path(), draft, {}, &error), 0); // never twice
    }

    void groupsRollUpToTheLowestLevel() {
        QCOMPARE(Classbook::rollUp({"Proficient", "Developing", "Extending"}), QString("Developing"));
        QCOMPARE(Classbook::rollUp({"Proficient", "", "Extending"}), QString("Proficient")); // unset ones left out
        QCOMPARE(Classbook::rollUp({"", ""}), QString());
    }

    void setupAddsCanvasGroupsToExistingPlans() {
        QTemporaryDir dir;
        const QString plan = "# my notes stay\n\n[[objective]]\nid = \"bits\"\ntitle = \"Bits and Binary\"\nidea = \"\"\n\n"
                             "[[objective]]\nid = \"net\"\ntitle = \"Renamed by me\"\ncanvas_assignment = \"32\"\n\n"
                             "[[objective]]\nid = \"mine\"\ntitle = \"My own objective\"\n";
        write(dir.filePath("plan.toml"), plan.toUtf8());
        Importer::ClassDraft draft;
        Importer::DraftObjective a{"bits-and-binary", "Bits and Binary", "", QDate(), "31", 10, "Digital Information"};
        Importer::DraftObjective b{"networks", "Networks", "", QDate(), "32", 10, "The Internet"};
        draft.objectives = {a, b};
        QString error;
        QCOMPARE(Importer::addGroups(dir.path(), draft, &error), 2);
        const ClassInfo info = Classbook::loadClass(dir.path());
        QCOMPARE(info.objectives.size(), 3);
        QCOMPARE(info.objectives[0].group, QString("Digital Information"));
        QCOMPARE(info.objectives[1].group, QString("The Internet"));
        QCOMPARE(info.objectives[2].group, QString());
        QFile file(dir.filePath("plan.toml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("# my notes stay"));
        QCOMPARE(Importer::addGroups(dir.path(), draft, &error), 0); // never twice
    }

    void studentPageSuggestsTheLowestOldestFirst() {
        QTemporaryDir dir;
        QString report, error;
        QVERIFY(Samples::init(dir.path(), false, &report, &error));
        QDir().mkpath(dir.filePath("demo"));
        write(dir.filePath("demo/class.toml"), "name = \"Demo\"\n");
        write(dir.filePath("demo/roster.toml"), "[[student]]\nname = \"Mara\"\n");
        write(dir.filePath("demo/plan.toml"),
              "[[objective]]\nid = \"a\"\ntitle = \"A\"\ngroup = \"Kinematics\"\nweek = \"2026-09-07\"\n"
              "[[objective]]\nid = \"b\"\ntitle = \"B\"\ngroup = \"Kinematics\"\nweek = \"2026-09-14\"\n"
              "[[objective]]\nid = \"c\"\ntitle = \"C\"\ngroup = \"Forces\"\nweek = \"2026-09-21\"\n"
              "[[objective]]\nid = \"d\"\ntitle = \"D\"\ngroup = \"Forces\"\nweek = \"2026-09-28\"\n"
              "[[objective]]\nid = \"e\"\ntitle = \"E\"\ngroup = \"Forces\"\nweek = \"2026-12-07\"\n");
        write(dir.filePath("demo/mastery.csv"), "date,student,objective,level,evidence\n"
                                                "2026-09-10,Mara,a,Proficient,\n2026-09-20,Mara,b,Developing,\n"
                                                "2026-09-25,Mara,c,Developing,\n");
        Backend backend(dir.path());
        backend.setClock(QDateTime(QDate(2026, 11, 9), QTime(8, 30)));
        backend.reload();
        backend.selectClassId("demo");
        QCOMPARE(backend.groupLevels().value("Mara|Kinematics").toString(), QString("Developing"));
        QCOMPARE(backend.groupLevels().value("Mara|Forces").toString(), QString("Developing"));
        const QVariantMap page = backend.studentReport("Mara");
        QCOMPARE(page.value("taught").toInt(), 4);
        QCOMPARE(page.value("assessed").toInt(), 3);
        const QVariantList focus = page.value("focus").toList();
        QCOMPARE(focus.size(), 2);
        QCOMPARE(focus[0].toMap().value("id").toString(), QString("b")); // same level: the older one first
        QCOMPARE(focus[0].toMap().value("target").toString(), QString("Proficient"));
        QCOMPARE(page.value("unassessed").toList().first().toMap().value("id").toString(), QString("d"));
        QCOMPARE(page.value("comingUp").toList().first().toMap().value("id").toString(), QString("e"));
        QCOMPARE(page.value("groups").toList().size(), 2);
    }

    void wordDocumentsGetMarkdownNames() {
        QTemporaryDir dir;
        write(dir.filePath("long-range-plan.docx"), "x");
        write(dir.filePath("syllabus.docx"), "x");
        write(dir.filePath("syllabus.md"), "# From Canvas\n");
        QCOMPARE(QFileInfo(Documents::markdownNameFor(dir.filePath("long-range-plan.docx"))).fileName(), QString("long-range-plan.md"));
        // syllabus.md is the Canvas syllabus, so the Word one gets its own name.
        QCOMPARE(QFileInfo(Documents::markdownNameFor(dir.filePath("syllabus.docx"))).fileName(), QString("syllabus-document.md"));
        // A copy OmaMastery made earlier is its own to replace.
        write(dir.filePath("long-range-plan.md"), "<!-- Markdown copy of long-range-plan.docx, made by OmaMastery -->\n");
        QCOMPARE(QFileInfo(Documents::markdownNameFor(dir.filePath("long-range-plan.docx"))).fileName(), QString("long-range-plan.md"));
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
