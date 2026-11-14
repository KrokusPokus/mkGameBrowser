#include "Config.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mkGameBrowser"));
    QApplication::setApplicationVersion(QStringLiteral(MKGAMEBROWSER_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("mkGameBrowser"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/mkGameBrowser.svg")));

    QTranslator translatorQt;
    if (translatorQt.load(QLocale::system(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&translatorQt);
    QTranslator translator;
    if (translator.load(QLocale::system(), QStringLiteral("mkGameBrowser"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        QApplication::installTranslator(&translator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "Browses a game collection by cover thumbnails."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption optGameInfo(QStringLiteral("gameinfo"),
                                         QCoreApplication::translate("main", "Use (and remember) this GameInfo folder."),
                                         QStringLiteral("folder"));
    parser.addOption(optGameInfo);
    parser.process(app);

    // ~/.config/mkProjects/mkGameBrowser.conf
    const QString sConfigDir = QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(QStringLiteral("mkProjects"));
    QDir().mkpath(sConfigDir);
    QSettings settings(QDir(sConfigDir).filePath(QStringLiteral("mkGameBrowser.conf")), QSettings::IniFormat);
    CConfig config;
    config.Load(settings);

    if (parser.isSet(optGameInfo))
        config.SetGameInfoDir(settings, QFileInfo(parser.value(optGameInfo)).absoluteFilePath());

    if (!config.HasValidGameInfoDir()) {
        const QString sDir = QFileDialog::getExistingDirectory(nullptr,
            QCoreApplication::translate("main", "Select the GameInfo folder (contains one subfolder per platform)"),
            config.sGameInfoDir.isEmpty() ? QDir::homePath() : config.sGameInfoDir);
        if (sDir.isEmpty())
            return 1;
        config.SetGameInfoDir(settings, sDir);
    }
    settings.sync();

    CMainWindow window(settings, config);
    window.show();
    // Load only after showing: then the final window width for line wrapping is known
    QTimer::singleShot(0, &window, &CMainWindow::LoadLibrary);
    return app.exec();
}
