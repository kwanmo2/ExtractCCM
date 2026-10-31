#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <cstdio>
#include <memory>

#include "ui/Language.h"
#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("ExtractCCM");

    QCommandLineParser parser;
    parser.setApplicationDescription("Extract a color correction matrix from a color chart image.");
    parser.addHelpOption();
    parser.addPositionalArgument("session", "Session file (.ccmproj) to open.");
    QCommandLineOption exportOpt("export-json", "Compute the session and write the result JSON, then exit.", "file");
    QCommandLineOption langOpt("lang", "UI language: en or ko (default: last used / system).", "code");
    parser.addOption(exportOpt);
    parser.addOption(langOpt);
    parser.process(app);

    Language lang = savedLanguage();
    if (parser.isSet(langOpt)) lang = parser.value(langOpt) == "ko" ? Language::Korean : Language::English;
    installLanguage(lang);

    const QStringList args = parser.positionalArguments();
    QString error;

    if (parser.isSet(exportOpt)) {
        MainWindow w;
        w.setBatchMode(true);
        if (args.isEmpty()) {
            std::fprintf(stderr, "--export-json needs a session file\n");
            return 2;
        }
        if (!w.loadSessionFile(args.first(), error) || !w.computeNow(error) ||
            !w.exportJsonTo(parser.value(exportOpt), error)) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        return 0;
    }

    // Heap-allocated: switching the language replaces the window with a new one.
    auto* w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    if (!args.isEmpty() && !w->loadSessionFile(args.first(), error))
        QMessageBox::warning(w, QObject::tr("ExtractCCM"), error);
    w->show();
    return app.exec();
}
