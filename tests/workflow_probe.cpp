// Small subprocess fixture: persistence and process locking are tested across
// actual executable lifetimes, not just by reopening an object in one process.
#include "domain/error.h"
#include "services/library.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace virgil;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("VirgilWorkflowProbe");
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    const QString mode = args[1];
    const QString root = args[2];
    try {
        if (mode == "create") {
            if (QFileInfo::exists(root) || !QDir().mkdir(root))
                throw Error("Probe requires a new disposable directory.");
            auto library = Library::create(root);
            const QString semester = library->createSemester("Winter 2026", "winter", QDate(2026, 10, 1), QDate(2027, 2, 17));
            library->createCourse(semester, "Mathematics", "MATH", "#2764C5");
            const QString source = root + "-source.txt";
            QFile file(source);
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write("offline study fixture\n") != 22)
                throw Error("Cannot create probe source.");
            file.close();
            library->importFile(source, "Winter 2026/Mathematics/Other Materials", "notes.txt");
            QTextStream(stdout) << library->snapshot().materials.first().id << '\n';
            return 0;
        }
        if (mode == "verify") {
            auto library = Library::open(root);
            const auto snapshot = library->refresh();
            if (snapshot.semesters.size() != 1 || snapshot.courses.size() != 1 || snapshot.materials.size() != 1
                || !snapshot.materials.first().available || snapshot.materials.first().courseId != snapshot.courses.first().id)
                throw Error("Persisted relationships did not match.");
            QFile file(library->materialPath(snapshot.materials.first().id));
            if (!file.open(QIODevice::ReadOnly) || file.readAll() != "offline study fixture\n")
                throw Error("Imported bytes did not match.");
            QTextStream(stdout) << snapshot.materials.first().id << '\n';
            return 0;
        }
        if (mode == "expect-locked") {
            try { auto library = Library::open(root); }
            catch (const Error& error) {
                return QString::fromUtf8(error.what()).contains("locked") ? 0 : 1;
            }
            return 1;
        }
        return 2;
    } catch (const std::exception& error) {
        QTextStream(stderr) << error.what() << '\n';
        return 1;
    }
}
