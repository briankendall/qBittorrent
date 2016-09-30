/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2014  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 *
 * Contact : chris@qbittorrent.org
 */

#include <QDebug>
#include <QScopedPointer>

#ifndef DISABLE_GUI
// GUI-only includes
#include <QFont>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSplashScreen>
#ifdef QBT_STATIC_QT
#include <QtPlugin>
#ifdef QBT_USES_QT5
Q_IMPORT_PLUGIN(QICOPlugin)
#else
Q_IMPORT_PLUGIN(qico)
#endif
#endif // QBT_STATIC_QT

#else
// NoGUI-only includes
#include <cstdio>
#ifdef Q_OS_UNIX
#include "unistd.h"
#endif
#endif // DISABLE_GUI

#ifdef Q_OS_UNIX
#include <signal.h>
#include <execinfo.h>
#include "stacktrace.h"
#endif // Q_OS_UNIX

#ifdef STACKTRACE_WIN
#include <signal.h>
#include "stacktrace_win.h"
#include "stacktrace_win_dlg.h"
#endif //STACKTRACE_WIN

#include <cstdlib>
#include <iostream>
#include "application.h"
#include "base/utils/misc.h"
#include "base/preferences.h"
#include "base/tristatebool.h"

#include "upgrade.h"

// Signal handlers
#if defined(Q_OS_UNIX) || defined(STACKTRACE_WIN)
void sigNormalHandler(int signum);
void sigAbnormalHandler(int signum);
// sys_signame[] is only defined in BSD
const char *sysSigName[] = {
    "", "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS", "SIGFPE", "SIGKILL",
    "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGSTKFLT", "SIGCHLD", "SIGCONT", "SIGSTOP",
    "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG", "SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO",
    "SIGPWR", "SIGUNUSED"
};
#endif

struct QBtCommandLineParameters
{
    bool showHelp;
#ifndef Q_OS_WIN
    bool showVersion;
#endif
#ifndef DISABLE_GUI
    bool noSplash;
#else
    bool shouldDaemonize;
#endif
    int webUiPort;
    QStringList torrents;
    QString savePath;
    TriStateBool addPaused;
    bool skipChecking;
    QString category;
    bool sequential;
    bool firstLastPiecePriority;
    QString unknownParameter;
    QString errorParameter;
    QString argumentError;

    QBtCommandLineParameters()
        : showHelp(false)
#ifndef Q_OS_WIN
        , showVersion(false)
#endif
#ifndef DISABLE_GUI
        , noSplash(Preferences::instance()->isSplashScreenDisabled())
#else
        , shouldDaemonize(false)
#endif
        , webUiPort(Preferences::instance()->getWebUiPort())
        , skipChecking(false)
        , sequential(false)
        , firstLastPiecePriority(false)
    {
    }

    QStringList paramList() const
    {
        QStringList result;
        // Because we're passing a string list to the currently running
        // qBittorrent process, we need some way of passing along the options
        // the user has specified. Here we place special strings that are
        // almost certainly not going to collide with a file path or URL
        // specified by the user, and placing them at the beginning of the
        // string listr so that they will be processed before the list of
        // torrent paths or URLs.

        if (!savePath.isEmpty())
            result.append(QString("@path=%1").arg(savePath));

        if (addPaused != TriStateBool::Undefined)
            result.append(QString("@addPaused=%1").arg(addPaused ? 1 : 0));

        if (skipChecking)
            result.append("@skipChecking");

        if (!category.isEmpty())
            result.append(QString("@category=%1").arg(category));

        if (sequential)
            result.append("@sequential");

        if (firstLastPiecePriority)
            result.append("@firstLastPiecePriority");

        result += torrents;
        return result;
    }
};

#ifndef DISABLE_GUI
void showSplashScreen();
#endif
void displayVersion();
void displayUsage(const QString &prg_name);
bool userAgreesWithLegalNotice();
void displayBadArgMessage(const QString &message);
QBtCommandLineParameters parseCommandLine();

// Main
int main(int argc, char *argv[])
{
    // We must save it here because QApplication constructor may change it
    bool isOneArg = (argc == 2);

    // Create Application
    QString appId = QLatin1String("qBittorrent-") + Utils::Misc::getUserIDString();
    QScopedPointer<Application> app(new Application(appId, argc, argv));

    const QBtCommandLineParameters params = parseCommandLine();

    if (!params.unknownParameter.isEmpty()) {
        displayBadArgMessage(QObject::tr("%1 is an unknown command line parameter.", "--random-parameter is an unknown command line parameter.")
                             .arg(params.unknownParameter));
        return EXIT_FAILURE;
    }

    if (!params.errorParameter.isEmpty()) {
        displayBadArgMessage(QObject::tr("%1 must be followed by a value.", "--random-parameter must be followed by a value.")
                             .arg(params.errorParameter));
        return EXIT_FAILURE;
    }

    if (!params.argumentError.isEmpty()) {
        displayBadArgMessage(params.argumentError);
        return EXIT_FAILURE;
    }

#ifndef Q_OS_WIN
    if (params.showVersion) {
        if (isOneArg) {
            displayVersion();
            return EXIT_SUCCESS;
        }
        else {
            displayBadArgMessage(QObject::tr("%1 must be the single command line parameter.")
                                 .arg(QLatin1String("-v (or --version)")));
            return EXIT_FAILURE;
        }
    }
#endif

    if (params.showHelp) {
        if (isOneArg) {
            displayUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        else {
            displayBadArgMessage(QObject::tr("%1 must be the single command line parameter.")
                                 .arg(QLatin1String("-h (or --help)")));
            return EXIT_FAILURE;
        }
    }

    if ((params.webUiPort > 0) && (params.webUiPort <= 65535)) {
        Preferences::instance()->setWebUiPort(params.webUiPort);
    }
    else {
        displayBadArgMessage(QObject::tr("%1 must specify the correct port (1 to 65535).")
                             .arg(QLatin1String("--webui-port")));
        return EXIT_FAILURE;
    }

    // Set environment variable
    if (!qputenv("QBITTORRENT", QByteArray(VERSION)))
        std::cerr << "Couldn't set environment variable...\n";

#ifndef DISABLE_GUI
    if (!userAgreesWithLegalNotice())
        return EXIT_SUCCESS;
#else
    if (!params.shouldDaemonize
        && isatty(fileno(stdin))
        && isatty(fileno(stdout))
        && !userAgreesWithLegalNotice())
        return EXIT_SUCCESS;
#endif

    // Check if qBittorrent is already running for this user
    if (app->isRunning()) {
#ifdef DISABLE_GUI
        if (params.shouldDaemonize) {
            displayBadArgMessage(QObject::tr("You cannot use %1: qBittorrent is already running for this user.")
                                 .arg(QLatin1String("-d (or --daemon)")));
            return EXIT_FAILURE;
        }
        else
#endif
        qDebug("qBittorrent is already running for this user.");

        Utils::Misc::msleep(300);
        app->sendParams(params.paramList());

        return EXIT_SUCCESS;
    }

#if defined(Q_OS_WIN) && defined(QBT_USES_QT5)
    // This affects only Windows apparently and Qt5.
    // When QNetworkAccessManager is instantiated it regularly starts polling
    // the network interfaces to see what's available and their status.
    // This polling creates jitter and high ping with wifi interfaces.
    // So here we disable it for lack of better measure.
    // It will also spew this message in the console: QObject::startTimer: Timers cannot have negative intervals
    // For more info see:
    // 1. https://github.com/qbittorrent/qBittorrent/issues/4209
    // 2. https://bugreports.qt.io/browse/QTBUG-40332
    // 3. https://bugreports.qt.io/browse/QTBUG-46015

    qputenv("QT_BEARER_POLL_TIMEOUT", QByteArray::number(-1));
#endif

#ifndef DISABLE_GUI
    if (!upgrade()) return EXIT_FAILURE;
#else
    if (!upgrade(!params.shouldDaemonize
                 && isatty(fileno(stdin))
                 && isatty(fileno(stdout)))) return EXIT_FAILURE;
#endif

    srand(time(0));
#ifdef DISABLE_GUI
    if (params.shouldDaemonize) {
        app.reset(); // Destroy current application
        if ((daemon(1, 0) == 0)) {
            app.reset(new Application(appId, argc, argv));
            if (app->isRunning()) {
                // Another instance had time to start.
                return EXIT_FAILURE;
            }
        }
        else {
            qCritical("Something went wrong while daemonizing, exiting...");
            return EXIT_FAILURE;
        }
    }
#else
    if (!params.noSplash)
        showSplashScreen();
#endif

#if defined(Q_OS_UNIX) || defined(STACKTRACE_WIN)
    signal(SIGINT, sigNormalHandler);
    signal(SIGTERM, sigNormalHandler);
    signal(SIGABRT, sigAbnormalHandler);
    signal(SIGSEGV, sigAbnormalHandler);
#endif

    return app->exec(params.torrents);
}

bool parseArgumentWithValue(QString& value, QString& errorArgument, const QStringList& appArguments, int& i,
  const QString& argName)
{
    if (appArguments[i].startsWith(argName + "=")) {
        value = appArguments[i].mid(argName.length() + 1);

        if (value.length() == 0)
            errorArgument = argName;

        return true;
    }
    else if (appArguments[i] == argName) {
        if ((i + 1) < appArguments.size()) {
            // NB: in this circumstance i will be incremented so that the for loop in parseCommandLine will
            // skip the next argument (since it is a value). Therefore it can't be assumed that i will be
            // the same value when parseArgumentWithValue() returns.
            i += 1;
            value = appArguments[i];
            return true;
        }
        else {
            // Invalid argument
            errorArgument = argName;
            return true;
        }
    }

    return false;
}

bool parseArgumentWithValue(QString& value, QString& errorArgument, const QStringList& appArguments, int& i,
  const QString& argName1, const QString& argName2)
{
    if (parseArgumentWithValue(value, errorArgument, appArguments, i, argName1)) {
        return true;
    }
    else {
        return parseArgumentWithValue(value, errorArgument, appArguments, i, argName2);
    }
}

QBtCommandLineParameters parseCommandLine()
{
    QBtCommandLineParameters result;
    QString value;
    QStringList appArguments = qApp->arguments();

    for (int i = 1; i < appArguments.size(); ++i) {
        const QString& arg = appArguments[i];

        if ((arg.startsWith("--") || arg.startsWith("-")) && !arg.endsWith(".torrent")) {
            //Parse known parameters
            if ((arg == QLatin1String("-h")) || (arg == QLatin1String("--help"))) {
                result.showHelp = true;
            }
#ifndef Q_OS_WIN
            else if ((arg == QLatin1String("-v")) || (arg == QLatin1String("--version"))) {
                result.showVersion = true;
            }
#endif
            else if (parseArgumentWithValue(value, result.errorParameter, appArguments, i, "--webui-port")) {
                result.webUiPort = value.toInt();
            }
#ifndef DISABLE_GUI
            else if (arg == QLatin1String("--no-splash")) {
                result.noSplash = true;
            }
#else
            else if ((arg == QLatin1String("-d")) || (arg == QLatin1String("--daemon"))) {
                result.shouldDaemonize = true;
            }
#endif
            else if (parseArgumentWithValue(value, result.errorParameter, appArguments, i, "-p", "--path")) {
                result.savePath = value;
            }
            else if (parseArgumentWithValue(value, result.errorParameter, appArguments, i, "--status")) {
                if (value == "started") {
                    result.addPaused = false;
                }
                else if (value == "paused") {
                    result.addPaused = true;
                }
                else {
                    result.argumentError = QObject::tr("valid values for --status are 'started' and 'paused'");
                }
            }
            else if (arg == QLatin1String("--skip-hash-check")) {
                result.skipChecking = true;
            }
            else if (parseArgumentWithValue(value, result.errorParameter, appArguments, i, "--category")) {
                result.category = value;
            }
            else if (arg == QLatin1String("--sequential")) {
                result.sequential = true;
            }
            else if (arg == QLatin1String("--first-and-last")) {
                result.firstLastPiecePriority = true;
            }
            else {
                //Unknown argument
                result.unknownParameter = arg;
                break;
            }
        }
        else {
            QFileInfo torrentPath;
            torrentPath.setFile(arg);

            if (torrentPath.exists())
                result.torrents += torrentPath.absoluteFilePath();
            else
                result.torrents += arg;
        }
    }

    return result;
}

#if defined(Q_OS_UNIX) || defined(STACKTRACE_WIN)
void sigNormalHandler(int signum)
{
#if !defined Q_OS_WIN && !defined Q_OS_HAIKU
    const char str1[] = "Catching signal: ";
    const char *sigName = sysSigName[signum];
    const char str2[] = "\nExiting cleanly\n";
    write(STDERR_FILENO, str1, strlen(str1));
    write(STDERR_FILENO, sigName, strlen(sigName));
    write(STDERR_FILENO, str2, strlen(str2));
#endif // !defined Q_OS_WIN && !defined Q_OS_HAIKU
    signal(signum, SIG_DFL);
    qApp->exit();  // unsafe, but exit anyway
}

void sigAbnormalHandler(int signum)
{
#if !defined Q_OS_WIN && !defined Q_OS_HAIKU
    const char str1[] = "\n\n*************************************************************\nCatching signal: ";
    const char *sigName = sysSigName[signum];
    const char str2[] = "\nPlease file a bug report at http://bug.qbittorrent.org and provide the following information:\n\n"
    "qBittorrent version: " VERSION "\n";
    write(STDERR_FILENO, str1, strlen(str1));
    write(STDERR_FILENO, sigName, strlen(sigName));
    write(STDERR_FILENO, str2, strlen(str2));
    print_stacktrace();  // unsafe
#endif // !defined Q_OS_WIN && !defined Q_OS_HAIKU
#ifdef STACKTRACE_WIN
    StraceDlg dlg;  // unsafe
    dlg.setStacktraceString(straceWin::getBacktrace());
    dlg.exec();
#endif // STACKTRACE_WIN
    signal(signum, SIG_DFL);
    raise(signum);
}
#endif // defined(Q_OS_UNIX) || defined(STACKTRACE_WIN)

#ifndef DISABLE_GUI
void showSplashScreen()
{
    QPixmap splash_img(":/icons/skin/splash.png");
    QPainter painter(&splash_img);
    QString version = VERSION;
    painter.setPen(QPen(Qt::white));
    painter.setFont(QFont("Arial", 22, QFont::Black));
    painter.drawText(224 - painter.fontMetrics().width(version), 270, version);
    QSplashScreen *splash = new QSplashScreen(splash_img);
    splash->show();
    QTimer::singleShot(1500, splash, SLOT(deleteLater()));
    qApp->processEvents();
}
#endif

void displayVersion()
{
    std::cout << qPrintable(qApp->applicationName()) << " " << VERSION << std::endl;
}

QString makeUsage(const QString &prg_name)
{
    QString text;
    QLatin1String tab("    ");

    text += QObject::tr("Usage:") + QLatin1Char('\n');
#ifndef Q_OS_WIN
    text += tab + prg_name + QLatin1String(" (-v | --version)") + QLatin1Char('\n');
#endif
    text += tab + prg_name + QLatin1String(" (-h | --help)") + QLatin1Char('\n');
    text += tab + prg_name + QLatin1String(" [options]")
            + QLatin1String(" [(<filename> | <url>)...]") + QLatin1Char('\n');
    text += QLatin1Char('\n');
    text += QObject::tr("Options:") + QLatin1Char('\n');
#ifndef Q_OS_WIN
    text += tab + QLatin1String("-v | --version                 ")
         + QObject::tr("Displays program version") + QLatin1Char('\n');
#endif
    text += tab + QLatin1String("-h | --help                    ")
         + QObject::tr("Displays this help message") + QLatin1Char('\n');
    text += tab + QLatin1String("--webui-port=<port>            ")
         + QObject::tr("Changes the Web UI port (current: %1)").arg(QString::number(Preferences::instance()->getWebUiPort()))
         + QLatin1Char('\n');
#ifndef DISABLE_GUI
    text += tab + QLatin1String("--no-splash                    ")
         + QObject::tr("Disable splash screen") + QLatin1Char('\n');
#else
    text += tab + QLatin1String("-d | --daemon                  ")
         + QObject::tr("Run in daemon-mode (background)") + QLatin1Char('\n');
#endif
    text += tab + QLatin1String("files or urls                  ")
         + QObject::tr("Downloads the torrents passed by the user") + QLatin1Char('\n');
    text += QLatin1Char('\n');
    text += QObject::tr("Options when passing in torrent files or URLs:") + QLatin1Char('\n');
    text += tab + QLatin1String("-p | --path <path>             ")
         + QObject::tr("Torrent save path") + QLatin1Char('\n');
    text += tab + QLatin1String("--status=<started|paused>      ")
         + QObject::tr("Add torrents as started or paused") + QLatin1Char('\n');
    text += tab + QLatin1String("--skip-hash-check              ")
         + QObject::tr("Skip hash check") + QLatin1Char('\n');
    text += tab + QLatin1String("--category=<category name>     ")
         + QObject::tr("Assign torrents to category") + QLatin1Char('\n');
    text += tab + QLatin1String("--sequential                   ")
         + QObject::tr("Download torrents in sequential order") + QLatin1Char('\n');
    text += tab + QLatin1String("--first-and-last               ")
         + QObject::tr("Download first and last pieces first") + QLatin1Char('\n');
    text += QLatin1Char('\n');
    text += QObject::tr("Using one or more of the above options will prevent qBittorrent from opening\n"
                        "the \"Add New Torrent\" dialog and will immediately add the torrent. It will use\n"
                        "the application settings unless specified otherwise with these options.") + QLatin1Char('\n');

    return text;
}

void displayUsage(const QString& prg_name)
{
#ifndef Q_OS_WIN
    std::cout << qPrintable(makeUsage(prg_name)) << std::endl;
#else
    QMessageBox msgBox(QMessageBox::Information, QObject::tr("Help"), makeUsage(prg_name), QMessageBox::Ok);
    msgBox.show(); // Need to be shown or to moveToCenter does not work
    msgBox.move(Utils::Misc::screenCenter(&msgBox));
    msgBox.exec();
#endif
}

void displayBadArgMessage(const QString& message)
{
    QString help = QObject::tr("Run application with -h option to read about command line parameters.");
#ifdef Q_OS_WIN
    QMessageBox msgBox(QMessageBox::Critical, QObject::tr("Bad command line"),
                       message + QLatin1Char('\n') + help, QMessageBox::Ok);
    msgBox.show(); // Need to be shown or to moveToCenter does not work
    msgBox.move(Utils::Misc::screenCenter(&msgBox));
    msgBox.exec();
#else
    std::cerr << qPrintable(QObject::tr("Bad command line: "));
    std::cerr << qPrintable(message) << std::endl;
    std::cerr << qPrintable(help) << std::endl;
#endif
}

bool userAgreesWithLegalNotice()
{
    Preferences* const pref = Preferences::instance();
    if (pref->getAcceptedLegal()) // Already accepted once
        return true;

#ifdef DISABLE_GUI
    std::cout << std::endl << "*** " << qPrintable(QObject::tr("Legal Notice")) << " ***" << std::endl;
    std::cout << qPrintable(QObject::tr("qBittorrent is a file sharing program. When you run a torrent, its data will be made available to others by means of upload. Any content you share is your sole responsibility.\n\nNo further notices will be issued.")) << std::endl << std::endl;
    std::cout << qPrintable(QObject::tr("Press %1 key to accept and continue...").arg("'y'")) << std::endl;
    char ret = getchar(); // Read pressed key
    if (ret == 'y' || ret == 'Y') {
        // Save the answer
        pref->setAcceptedLegal(true);
        return true;
    }
#else
    QMessageBox msgBox;
    msgBox.setText(QObject::tr("qBittorrent is a file sharing program. When you run a torrent, its data will be made available to others by means of upload. Any content you share is your sole responsibility.\n\nNo further notices will be issued."));
    msgBox.setWindowTitle(QObject::tr("Legal notice"));
    msgBox.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
    QAbstractButton *agree_button = msgBox.addButton(QObject::tr("I Agree"), QMessageBox::AcceptRole);
    msgBox.show(); // Need to be shown or to moveToCenter does not work
    msgBox.move(Utils::Misc::screenCenter(&msgBox));
    msgBox.exec();
    if (msgBox.clickedButton() == agree_button) {
        // Save the answer
        pref->setAcceptedLegal(true);
        return true;
    }
#endif

    return false;
}
