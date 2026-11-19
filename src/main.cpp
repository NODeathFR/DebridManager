// Debrid Manager — client RealDebrid / AllDebrid (C++ / Qt 6)

#include "core/Associations.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QPointer>
#include <QThread>
#include <QUrl>

static QString userName() { return qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER")); }

static QString serverName() { return QStringLiteral("DebridManager-%1").arg(userName()); }

// Verrou « une seule copie du programme » (dossier temporaire : propre à
// chaque compte Windows). Libéré automatiquement si le programme est tué.
static QString lockPath()
{
    return QDir(QDir::tempPath()).filePath(QStringLiteral("DebridManager-%1.lock").arg(userName()));
}

// Fichiers donnés en chemin relatif : rendus absolus, car la copie déjà
// ouverte n'a pas le même dossier courant.
static QStringList absoluteArgs(const QStringList& args)
{
    QStringList out;
    for (QString a : args) {
        // Certains gestionnaires de fichiers Linux transmettent « file:///chemin ».
        if (a.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
            a = QUrl(a).toLocalFile();
        const bool isLink = a.startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive)
            || a.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || a.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
        out << (!isLink && QFileInfo::exists(a) ? QFileInfo(a).absoluteFilePath() : a);
    }
    return out;
}

// Envoie les arguments à la copie déjà ouverte. Vrai si elle les a bien reçus.
static bool sendToRunningInstance(const QStringList& args)
{
    QLocalSocket sock;
    sock.connectToServer(serverName());
    if (!sock.waitForConnected(300))
        return false;
    sock.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact) + '\n');
    if (!sock.waitForBytesWritten(3000))
        return false;
    // Accusé de réception : la copie ouverte peut être encore en train de
    // démarrer (chargement de la session) ; on lui laisse le temps.
    const bool ok = sock.waitForReadyRead(20000) && sock.readAll().startsWith("ok");
    sock.disconnectFromServer();
    return ok;
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DebridManager"));
    QApplication::setApplicationDisplayName(QStringLiteral("Debrid Manager"));
    QApplication::setOrganizationName(QStringLiteral("DebridManager"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(Icons::icon(QStringLiteral("app")));
    // Linux (Wayland) : le bureau retrouve l'icône et le nom de la fenêtre
    // grâce au fichier debridmanager.desktop.
    QGuiApplication::setDesktopFileName(QStringLiteral("debridmanager"));
    QApplication::setQuitOnLastWindowClosed(false);   // l'icône de notification peut rester

    const QStringList args = absoluteArgs(QApplication::arguments().mid(1));

    // Instance unique. Plusieurs .torrent ouverts d'un coup depuis
    // l'Explorateur lancent une copie du programme par fichier, presque en
    // même temps : la première prend le verrou, les autres attendent qu'elle
    // soit prête et lui confient leurs fichiers.
    QLockFile lock(lockPath());
    lock.setStaleLockTime(0);   // verrou abandonné = programme qui n'existe plus
    {
        QElapsedTimer waited;
        waited.start();
        for (;;) {
            if (sendToRunningInstance(args))
                return 0;
            if (lock.tryLock(0))
                break;              // nous sommes la copie principale
            if (waited.elapsed() > 30000)
                return 1;           // copie principale bloquée : on n'en ouvre pas une seconde
            QThread::msleep(150);
        }
    }

    // Écoute dès maintenant (avant la fenêtre) : les copies lancées pendant
    // le démarrage se connectent et attendent leur accusé de réception.
    QLocalServer server;
    QLocalServer::removeServer(serverName());   // reste d'une copie tuée (Linux)
    server.listen(serverName());

    // Dossier portable déplacé : on remet à jour les associations .torrent /
    // magnet déjà activées (jamais d'activation sans accord).
    Associations::repairMoved();

    Theme::apply(app);

    MainWindow w;
    w.show();

    QPointer<MainWindow> window(&w);
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&server, window] {
        while (QLocalSocket* sock = server.nextPendingConnection()) {
            QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
            // Le message peut arriver en plusieurs morceaux : on attend la fin de ligne.
            QObject::connect(sock, &QLocalSocket::readyRead, sock, [sock, window] {
                if (!sock->canReadLine() || !window)
                    return;
                const QJsonArray arr = QJsonDocument::fromJson(sock->readLine()).array();
                sock->write("ok\n");
                sock->flush();
                QStringList received;
                for (const QJsonValue& v : arr)
                    if (!v.toString().isEmpty())
                        received << v.toString();
                if (received.isEmpty()) {   // simple relance : on ramène la fenêtre
                    window->showNormal();
                    window->raise();
                    window->activateWindow();
                } else {
                    window->handleExternal(received);
                }
            });
            if (sock->bytesAvailable() > 0)   // déjà reçu avant le branchement
                QMetaObject::invokeMethod(sock, &QLocalSocket::readyRead, Qt::QueuedConnection);
        }
    });
    w.handleExternal(args);
    return app.exec();
}
