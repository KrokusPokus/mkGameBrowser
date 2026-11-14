#include "Helpers.h"

// Taken from mkTools/shared/src/helpers.cpp (only the parts for launching programs/.desktop files)

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

#include <functional>

namespace {

    constexpr int kMaxInputLength = 8192;

    // Removes invisible/control Unicode characters that crafted web pages can inject into
    // copied text (zero-width chars, bidi overrides as in
    // "Trojan Source" attacks, BOM). Real RTL script (Arabic/Hebrew) is
    // NOT affected - those are normal letters, not control characters.
    QString stripInvisibleAndControlChars(const QString &input) {
        QString out;
        out.reserve(input.size());
        for (QChar c : input) {
            const ushort u = c.unicode();
            if (c == '\n' || c == '\r' || c == '\t') { out += c; continue; }
            if (u < 0x20 || (u >= 0x7F && u <= 0x9F)) continue;
            if (u == 0x200B || u == 0x200C || u == 0x200D || u == 0xFEFF ||
                (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069))
                continue;
            out += c;
        }
        return out;
    }

    // A single pass over 'input': substituted values are NOT scanned again,
    // so chaining ($FOO expands to a string that again contains ${...}
    // and gets resolved again) is structurally impossible, not merely limited.
    QString expandVariablesOnce(const QString &input,
                                const QRegularExpression &regex,
                                const std::function<QString(const QRegularExpressionMatch&)> &resolve) {

        auto it = regex.globalMatch(input);

        if (!it.hasNext()) {
            return input;
        }

        QString result;
        result.reserve(input.size());
        qsizetype lastEnd = 0;
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            result += input.sliced(lastEnd, m.capturedStart() - lastEnd);
            result += resolve(m);
            lastEnd = m.capturedEnd();
        }
        result += input.sliced(lastEnd);
        return result;
    }

    // Expands a single line purely in memory
    QString expandSingleLine(QString line) {
        line = line.trimmed();
        if (line.isEmpty()) return QString();

        // 1. Remove quotes
        if (line.length() >= 2 &&
            ((line.startsWith('"') && line.endsWith('"')) ||
             (line.startsWith('\'') && line.endsWith('\''))))
        {
            line = line.mid(1, line.length() - 2).trimmed();
            if (line.isEmpty()) return QString();
        }

        // 2. Convert file:// URLs to local paths
        QUrl url(line);
        if (url.isLocalFile()) {
            line = url.toLocalFile();
        }

#ifdef Q_OS_WIN
        static const QRegularExpression winEnvRegex(R"(%([A-Za-z0-9_()]+)%)");
        line = expandVariablesOnce(line, winEnvRegex, [](const QRegularExpressionMatch &m) {
            const QByteArray name = m.captured(1).toUtf8();
            if (!qEnvironmentVariableIsSet(name.constData())) return m.captured(0);
            return qEnvironmentVariable(name.constData());
        });
#else
        // Applies to Linux AND macOS (POSIX semantics are identical)
        if (line == "~") {
            line = QDir::homePath();
        } else if (line.startsWith("~/") || line.startsWith("~\\")) {
            line = QDir::homePath() + line.mid(1);
        }

        static const QRegularExpression posixEnvRegex(R"(\$\{([A-Za-z0-9_]+)\}|\$([A-Za-z0-9_]+))");
        line = expandVariablesOnce(line, posixEnvRegex, [](const QRegularExpressionMatch &m) {
            const QString varName = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
            if (varName == "HOME") return QDir::homePath();
            const QByteArray name = varName.toUtf8();
            if (!qEnvironmentVariableIsSet(name.constData())) return m.captured(0);
            return qEnvironmentVariable(name.constData());
        });
#endif

        return QDir::cleanPath(line);
    }


} // namespace

namespace {
// ---------------------------------------------------------------------------------------------
// Split the Exec line according to the Desktop Entry Specification and replace field codes
// https://specifications.freedesktop.org/desktop-entry-spec/latest/exec-variables.html
// ---------------------------------------------------------------------------------------------

struct ExecToken {
    QString text;
    bool quoted = false; // argument was (fully or partially) inside "..."
};

// Stage 1: string escapes that apply to every value of type "string" (\s \n \t \r \\)
QString unescapeDesktopString(const QString &in) {
    QString out;
    out.reserve(in.size());
    for (qsizetype i = 0; i < in.size(); ++i) {
        const QChar c = in.at(i);
        if (c == '\\' && i + 1 < in.size()) {
            const QChar n = in.at(i + 1);
            if (n == 's')  { out += ' ';  ++i; continue; }
            if (n == 'n')  { out += '\n'; ++i; continue; }
            if (n == 't')  { out += '\t'; ++i; continue; }
            if (n == 'r')  { out += '\r'; ++i; continue; }
            if (n == '\\') { out += '\\'; ++i; continue; }
        }
        out += c;
    }
    return out;
}

// Stage 2: split into arguments. Separators are spaces; inside "..." \" \` \$ \\ are escapes.
// Unknown escapes are leniently kept verbatim.
QList<ExecToken> splitDesktopExec(const QString &exec) {
    QList<ExecToken> tokens;
    ExecToken current;
    bool inQuotes = false;
    bool hasToken = false;

    for (qsizetype i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (inQuotes) {
            if (c == '\\' && i + 1 < exec.size() && QStringLiteral("\"`$\\").contains(exec.at(i + 1))) {
                current.text += exec.at(++i);
            } else if (c == '"') {
                inQuotes = false;
            } else {
                current.text += c;
            }
        } else if (c == ' ' || c == '\t' || c == '\n') {
            if (hasToken) {
                tokens << current;
                current = ExecToken();
                hasToken = false;
            }
        } else if (c == '"') {
            inQuotes = true;
            current.quoted = true;
            hasToken = true;
        } else {
            current.text += c;
            hasToken = true;
        }
    }
    if (hasToken) tokens << current;
    return tokens;
}

// Does the argument contain one of the field codes in 'codes' (e.g. "fu")? %% is skipped correctly.
bool containsFieldCode(const QString &token, const QString &codes) {
    for (qsizetype i = 0; i + 1 < token.size(); ++i) {
        if (token.at(i) != '%') continue;
        if (codes.contains(token.at(i + 1))) return true;
        ++i; // "%%" or other code -> skip next character
    }
    return false;
}

// Replaces the field codes inside an argument. Returns false = argument is dropped
// (%f/%u without a file, as well as %F/%U/%i, which are only valid as standalone arguments).
bool expandFieldCodes(const QString &token, const QString *file, const DesktopEntry &entry, QString &out) {
    out.clear();
    for (qsizetype i = 0; i < token.size(); ++i) {
        const QChar c = token.at(i);
        if (c != '%' || i + 1 >= token.size()) {
            out += c;
            continue;
        }
        const QChar code = token.at(++i);
        switch (code.unicode()) {
        case '%': out += '%'; break;
        case 'f':
        case 'u':
            if (!file) return false;
            out += *file;
            break;
        case 'c': out += entry.name; break;
        case 'k': out += QFileInfo(entry.path).absoluteFilePath(); break;
        case 'd': case 'D': case 'n': case 'N': case 'v': case 'm':
            break; // deprecated -> remove
        case 'F': case 'U': case 'i':
            return false;
        default:
            out += '%';
            out += code;
            break;
        }
    }
    return true;
}
} // namespace

namespace Helpers {

    DesktopEntry getDesktopEntryById(const QString &id) {
        static const QStringList appDirs = QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
        for (const QString &dirPath : appDirs) {
            QFileInfo fileInfo(QDir(dirPath).filePath(id));
            DesktopEntry entry = getDesktopEntry(fileInfo);
            if (entry.isValid) {
                return entry;
            }
        }
        return DesktopEntry();
    }

    DesktopEntry getDesktopEntry(const QFileInfo &fileInfo) {

        static const QString localeName = QLocale::system().name().left(2); // "de", "fr", ...
        static const QString localKey = QString("Name[%1]=").arg(localeName);

        DesktopEntry currentEntry;

        QFile file(fileInfo.filePath());
        if (file.open(QIODevice::ReadOnly)) {
            QTextStream in(&file);

            QString iniName, iniNameLocalised;
            bool bInMainSection = false;

            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();

                if (line.startsWith('[') && line.endsWith(']')) {
                    if (bInMainSection) break;
                    bInMainSection = (line == "[Desktop Entry]");
                    continue;
                }

                if (!bInMainSection || line.startsWith('#')) continue;

                if (line.startsWith("Name=")) iniName = line.mid(5);
                else if (line.startsWith(localKey)) iniNameLocalised = line.mid(localKey.size());
                else if (line.startsWith("Type=")) currentEntry.type = line.mid(5);
                // URL (catches both "URL=" and "URL[$e]=")
                else if (line.startsWith("URL=") || line.startsWith("URL[")) {
                    int idx = line.indexOf('=');
                    if (idx != -1) currentEntry.url = line.mid(idx + 1).trimmed();
                }
                else if (line.startsWith("Icon=")) currentEntry.icon = line.mid(5);
                else if (line.startsWith("Exec=")) currentEntry.exec = line.mid(5);
                else if (line.startsWith("Path=")) currentEntry.workDir = line.mid(5);
                else if (line.startsWith("Terminal=true")) currentEntry.useTerminal = true;
                else if (line.startsWith("Hidden=true")) {
                    return DesktopEntry();
                }
            }

            currentEntry.name = iniNameLocalised.isEmpty() ? iniName : iniNameLocalised;
            currentEntry.isValid = !currentEntry.name.isEmpty();
            currentEntry.id = fileInfo.fileName();
            currentEntry.path = fileInfo.filePath();

            if (currentEntry.isValid) {
                return currentEntry;
            }
        }

        return DesktopEntry();
    }

    void openFileListWithHandler(const QString &handlerApp, const QStringList &fileList) {
        if (handlerApp.endsWith(".desktop", Qt::CaseInsensitive)) {
            launchDesktopFile(getDesktopEntryById(handlerApp), fileList);
            return;
        }

        if (!handlerApp.contains('/') && !handlerApp.contains('\\')) {
            QString localAppPath = QDir(QCoreApplication::applicationDirPath()).filePath(handlerApp);
            if (QFile::exists(localAppPath)) {
                QProcess::startDetached(QDir::toNativeSeparators(localAppPath), fileList);
                return;
            }

            QString absolutePath = QStandardPaths::findExecutable(handlerApp);
            if (!absolutePath.isEmpty()) {
                // bool QProcess::startDetached(const QString &program, const QStringList &arguments = {}, const QString &workingDirectory = QString(), qint64 *pid = nullptr)
                QProcess::startDetached(QDir::toNativeSeparators(absolutePath), fileList);
                return;
            }
        } else if (QFile::exists(handlerApp)) {
            QProcess::startDetached(QDir::toNativeSeparators(handlerApp), fileList);
            return;
        }
    }

    // Converts the Exec line into a list of invocations (program + arguments).
    // Multiple invocations only occur for %f/%u with several files (once per file, per the specification).
    QList<QStringList> expandDesktopExec(const DesktopEntry &entry, const QStringList &files) {
        const QList<ExecToken> tokens = splitDesktopExec(unescapeDesktopString(entry.exec));
        if (tokens.isEmpty()) return {};

        bool hasListCode = false;   // %F / %U
        bool hasSingleCode = false; // %f / %u
        for (qsizetype t = 1; t < tokens.size(); ++t) {
            const ExecToken &tok = tokens.at(t);
            if (!tok.quoted && (tok.text == "%F" || tok.text == "%U")) {
                hasListCode = true;
            } else if (containsFieldCode(tok.text, QStringLiteral("fu"))) {
                hasSingleCode = true;
            }
        }

        auto buildInvocation = [&](const QString *file) {
            QStringList args;
            args << tokens.first().text; // program: no field codes
            for (qsizetype t = 1; t < tokens.size(); ++t) {
                const ExecToken &tok = tokens.at(t);
                if (!tok.quoted && (tok.text == "%F" || tok.text == "%U")) {
                    args << files;
                    continue;
                }
                if (!tok.quoted && tok.text == "%i") {
                    if (!entry.icon.isEmpty()) args << QStringLiteral("--icon") << entry.icon;
                    continue;
                }
                QString expanded;
                if (!expandFieldCodes(tok.text, file, entry, expanded)) continue;
                if (expanded.isEmpty() && !tok.text.isEmpty() && !tok.quoted) continue; // e.g. just "%d"
                args << expanded;
            }
            return args;
        };

        QList<QStringList> invocations;
        if (hasListCode) {
            invocations << buildInvocation(files.isEmpty() ? nullptr : &files.first());
        } else if (hasSingleCode) {
            if (files.isEmpty()) {
                invocations << buildInvocation(nullptr);
            } else {
                for (const QString &file : files) invocations << buildInvocation(&file);
            }
        } else {
            // No file field code: append files (previous behavior, similar to KDE)
            QStringList args = buildInvocation(nullptr);
            args << files;
            invocations << args;
        }
        return invocations;
    }

    void launchDesktopFile(const DesktopEntry &info, const QStringList &fileList) {
        if (!info.isValid) return;

        // ==========================================
        // 1. Handling of Type=Link or linked URLs
        // ==========================================
        if (info.type.compare(QLatin1String("Link"), Qt::CaseInsensitive) == 0 || !info.url.isEmpty()) {
            QString rawUrl = info.url.trimmed();
            if (rawUrl.isEmpty()) return;

            // 1. Check directly whether it already is a valid network URL with a scheme (http, https, ftp, etc.)
            QUrl url(rawUrl);
            if (!url.scheme().isEmpty() && url.isValid()) {
                QDesktopServices::openUrl(url);
                return;
            }

            // 2. Only if there is no scheme, resolve paths (~/, $HOME, relative paths)
            QString urlStr = Helpers::expandPath(rawUrl);

            QFileInfo fileInfo(urlStr);
            if (fileInfo.isRelative()) {
                QDir desktopFileDir = QFileInfo(info.path).absoluteDir();
                urlStr = desktopFileDir.absoluteFilePath(urlStr);
            }

            // Explicitly convert local paths to file URLs
            url = QUrl::fromLocalFile(urlStr);
            if (!url.isValid()) {
                url = QUrl::fromUserInput(urlStr);
            }

            if (url.isValid()) {
                QDesktopServices::openUrl(url);
            } else {
                qWarning() << "launchDesktopFile(): Invalid URL:" << urlStr;
            }
            return;
        }

        // ==========================================
        // 2. Handling of Type=Application (normal Exec launch)
        // ==========================================
        if (info.exec.isEmpty()) return;

        for (QStringList args : expandDesktopExec(info, fileList)) {
            if (args.isEmpty()) continue;
            const QStringList invocation = args;
            QString program = args.takeFirst();

            program = Helpers::expandPath(program);
            if (!QFileInfo(program).isAbsolute()) {
                program = QStandardPaths::findExecutable(program);
                if (program.isEmpty()) {
                    qWarning() << "launchDesktopFile(): Program not found:" << invocation.first();
                    continue;
                }
            }

            // ==========================================
            // Terminal-Handling
            // ==========================================
            if (info.useTerminal) {
                QString terminal;
                // Prioritized list of known Linux terminals (Konsole first because of KDE)
                const QStringList terminalCandidates = {
                    "konsole", "gnome-terminal", "xfce4-terminal", "alacritty", "kitty", "xterm"
                };

                for (const QString &candidate : terminalCandidates) {
                    terminal = QStandardPaths::findExecutable(candidate);
                    if (!terminal.isEmpty()) {
                        break;
                    }
                }

                if (!terminal.isEmpty()) {
                    QStringList terminalArgs;

                    // Older/lightweight terminals like xterm/alacritty usually need '-e'
                    // Modern desktop terminals (konsole, gnome-terminal) prefer '--'
                    if (terminal.endsWith("gnome-terminal")) {
                        terminalArgs << "--" << program << args;
                    } else {
                        terminalArgs << "-e" << program << args;
                    }

                    // The terminal becomes the main program, the app moves into the arguments
                    program = terminal;
                    args = terminalArgs;
                } else {
                    // Fallback: if no terminal at all was found,
                    // log a warning and launch it normally in the background.
                    qWarning("Could not find a terminal emulator. Launching app normally.");
                }
            }
            // ==========================================

            QString workDir = Helpers::expandPath(info.workDir.trimmed());
            if (workDir.startsWith('"') && workDir.endsWith('"'))
                workDir = workDir.mid(1, workDir.length() - 2);
            if (workDir.isEmpty() || !QDir(workDir).exists())
                workDir = QFileInfo(program).absolutePath();
            qDebug() << "launchDesktopFile() program:" << program << "args:" << args;
            QProcess::startDetached(program, args, workDir);
        }
    }

    QString expandPath(QString path) {
        if (path.size() > kMaxInputLength) {
            path.truncate(kMaxInputLength);
        }
        path = stripInvisibleAndControlChars(path);
        return expandSingleLine(path);
    }

} // namespace Helpers
