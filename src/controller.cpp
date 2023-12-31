#include "controller.h"

#include <qt5keychain/keychain.h>

#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QStringBuilder>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QMovie>
#include <QtGui/QPixmap>
#include <QtNetwork/QAuthenticator>
#include <QtNetwork/QNetworkReply>

#include "csapi/account-data.h"
#include "csapi/content-repo.h"
#include "csapi/joining.h"
#include "csapi/logout.h"
#include "csapi/profile.h"
#include "events/eventcontent.h"
#include "events/roommessageevent.h"
#include "settings.h"
#include "spectralroom.h"
#include "spectraluser.h"
#include "utils.h"

Controller::Controller(QObject* parent) : QObject(parent) {
  QApplication::setQuitOnLastWindowClosed(false);

  Connection::setRoomType<SpectralRoom>();
  Connection::setUserType<SpectralUser>();

  connect(&m_ncm, &QNetworkConfigurationManager::onlineStateChanged, this,
          &Controller::isOnlineChanged);

  QTimer::singleShot(0, this, [=] { invokeLogin(); });
}

Controller::~Controller() {
  for (auto c : m_connections) {
    c->stopSync();
    c->saveState();
  }
}

inline QString accessTokenFileName(const AccountSettings& account) {
  QString fileName = account.userId();
  fileName.replace(':', '_');
  return QStandardPaths::writableLocation(
             QStandardPaths::AppLocalDataLocation) +
         '/' + fileName;
}

Connection* Controller::newConnection() {
  return new Connection(this);
}

QString Controller::generateDeviceName(const QString& productName) {
  return productName + " " + QSysInfo::machineHostName() + " " +
         QSysInfo::productType() + " " + QSysInfo::productVersion() + " " +
         QSysInfo::currentCpuArchitecture();
}

void Controller::finishLogin(Connection* conn, QString deviceName) {
  qDebug() << "Setting up" << conn->userId() << "'s" << deviceName;

  AccountSettings account(conn->userId());
  account.setKeepLoggedIn(true);
  account.clearAccessToken();  // Drop the legacy - just in case
  account.setHomeserver(conn->homeserver());
  account.setDeviceId(conn->deviceId());
  account.setDeviceName(deviceName);
  if (!saveAccessTokenToKeyChain(account, conn->accessToken()))
    qWarning() << "Couldn't save access token";
  account.sync();
  addConnection(conn);
  setConnection(conn);
}

void Controller::logout(Connection* conn) {
  if (!conn) {
    qCritical() << "Attempt to logout null connection";
    return;
  }

  SettingsGroup("Accounts").remove(conn->userId());
  QFile(accessTokenFileName(AccountSettings(conn->userId()))).remove();

  QKeychain::DeletePasswordJob job(qAppName());
  job.setAutoDelete(true);
  job.setKey(conn->userId());
  QEventLoop loop;
  QKeychain::DeletePasswordJob::connect(&job, &QKeychain::Job::finished, &loop,
                                        &QEventLoop::quit);
  job.start();
  loop.exec();

  auto logoutJob = conn->callApi<LogoutJob>();
  connect(logoutJob, &LogoutJob::finished, conn, [=] {
    conn->stopSync();
    emit conn->stateChanged();
    emit conn->loggedOut();
    if (m_connections.isEmpty()) {
      emit firstTimeLogin();
      return;
    }
    setConnection(m_connections[0]);
  });
  connect(logoutJob, &LogoutJob::failure, this, [=] {
    emit errorOccured("Server-side Logout Failed", logoutJob->errorString());
  });
}

void Controller::addConnection(Connection* c) {
  Q_ASSERT_X(c, __FUNCTION__, "Attempt to add a null connection");

  m_connections += c;

  c->setLazyLoading(true);

  connect(c, &Connection::syncDone, this, [=] {
    setBusy(false);

    c->sync(30000);
    c->saveState();
  });
  connect(c, &Connection::loggedOut, this, [=] { dropConnection(c); });
  connect(&m_ncm, &QNetworkConfigurationManager::onlineStateChanged,
          [=](bool status) {
            if (!status) {
              return;
            }

            c->stopSync();
            c->sync(30000);
          });

  using namespace Quotient;

  setBusy(true);

  c->sync();

  emit connectionAdded(c);
}

void Controller::dropConnection(Connection* c) {
  Q_ASSERT_X(c, __FUNCTION__, "Attempt to drop a null connection");
  m_connections.removeOne(c);

  emit connectionDropped(c);
  c->deleteLater();
}

void Controller::invokeLogin() {
  using namespace Quotient;
  connect(this, &Controller::initialized, this, [=] {
    qDebug() << "Application initialized";

    if (m_connections.isEmpty()) {
      emit firstTimeLogin();
      return;
    }

    setConnection(m_connections[0]);
  });

  const auto accounts = SettingsGroup("Accounts").childGroups();
  m_init_connections_count = accounts.count();
  if (m_init_connections_count < 1) {
    emit initialized();
    return;
  }

  for (const auto& accountId : accounts) {
    AccountSettings account{accountId};
    if (!account.homeserver().isEmpty()) {
      auto accessToken = loadAccessTokenFromKeyChain(account);

      auto c = new Connection(account.homeserver(), this);
      auto deviceName = account.deviceName();
      connect(c, &Connection::connected, this, [=] {
        c->loadState();
        addConnection(c);
      });
      connect(c, &Connection::loginError, [=](QString error, QString) {
        emit errorOccured("Login Failed", error);
        logout(c);
      });
      connect(c, &Connection::networkError,
              [=](QString error, QString, int, int) {
                emit errorOccured("Network Error", error);
              });
      connect(c, &Connection::connected, this,
              &Controller::handleInitConnection);
      connect(c, &Connection::loginError, this,
              &Controller::handleInitConnection);

      c->assumeIdentity(account.userId(), accessToken, account.deviceId());
    }
  }
}

void Controller::handleInitConnection() {
  m_init_connections_count -= 1;

  if (m_init_connections_count < 1) {
    emit initialized();
  }
}

QByteArray Controller::loadAccessTokenFromFile(const AccountSettings& account) {
  QFile accountTokenFile{accessTokenFileName(account)};
  if (accountTokenFile.open(QFile::ReadOnly)) {
    if (accountTokenFile.size() < 1024)
      return accountTokenFile.readAll();

    qWarning() << "File" << accountTokenFile.fileName() << "is"
               << accountTokenFile.size()
               << "bytes long - too long for a token, ignoring it.";
  }
  qWarning() << "Could not open access token file"
             << accountTokenFile.fileName();

  return {};
}

QByteArray Controller::loadAccessTokenFromKeyChain(
    const AccountSettings& account) {
  qDebug() << "Read the access token from the keychain for "
           << account.userId();
  QKeychain::ReadPasswordJob job(qAppName());
  job.setAutoDelete(false);
  job.setKey(account.userId());
  QEventLoop loop;
  QKeychain::ReadPasswordJob::connect(&job, &QKeychain::Job::finished, &loop,
                                      &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error() == QKeychain::Error::NoError) {
    return job.binaryData();
  }

  qWarning() << "Could not read the access token from the keychain: "
             << qPrintable(job.errorString());
  // no access token from the keychain, try token file
  auto accessToken = loadAccessTokenFromFile(account);
  if (job.error() == QKeychain::Error::EntryNotFound) {
    if (!accessToken.isEmpty()) {
      qDebug() << "Migrating the access token from file to the keychain for "
               << account.userId();
      bool removed = false;
      bool saved = saveAccessTokenToKeyChain(account, accessToken);
      if (saved) {
        QFile accountTokenFile{accessTokenFileName(account)};
        removed = accountTokenFile.remove();
      }
      if (!(saved && removed)) {
        qDebug() << "Migrating the access token from the file to the keychain "
                    "failed";
      }
    }
  }

  return accessToken;
}

bool Controller::saveAccessTokenToFile(const AccountSettings& account,
                                       const QByteArray& accessToken) {
  // (Re-)Make a dedicated file for access_token.
  QFile accountTokenFile{accessTokenFileName(account)};
  accountTokenFile.remove();  // Just in case

  auto fileDir = QFileInfo(accountTokenFile).dir();
  if (!((fileDir.exists() || fileDir.mkpath(".")) &&
        accountTokenFile.open(QFile::WriteOnly))) {
    emit errorOccured("I/O Denied", "Cannot save access token.");
  } else {
    accountTokenFile.write(accessToken);
    return true;
  }
  return false;
}

bool Controller::saveAccessTokenToKeyChain(const AccountSettings& account,
                                           const QByteArray& accessToken) {
  qDebug() << "Save the access token to the keychain for " << account.userId();
  QKeychain::WritePasswordJob job(qAppName());
  job.setAutoDelete(false);
  job.setKey(account.userId());
  job.setBinaryData(accessToken);
  QEventLoop loop;
  QKeychain::WritePasswordJob::connect(&job, &QKeychain::Job::finished, &loop,
                                       &QEventLoop::quit);
  job.start();
  loop.exec();

  if (job.error()) {
    qWarning() << "Could not save access token to the keychain: "
               << qPrintable(job.errorString());
    return saveAccessTokenToFile(account, accessToken);
  }

  return true;
}

void Controller::joinRoom(Connection* c, const QString& alias) {
  if (!alias.contains(":"))
    return;

  auto knownServer = alias.mid(alias.indexOf(":") + 1);
  auto joinRoomJob = c->joinRoom(alias, QStringList{knownServer});
  joinRoomJob->connect(joinRoomJob, &JoinRoomJob::failure, [=] {
    emit errorOccured("Join Room Failed", joinRoomJob->errorString());
  });
}

void Controller::createRoom(Connection* c,
                            const QString& name,
                            const QString& topic) {
  auto createRoomJob =
      c->createRoom(Connection::PublishRoom, "", name, topic, QStringList());
  createRoomJob->connect(createRoomJob, &CreateRoomJob::failure, [=] {
    emit errorOccured("Create Room Failed", createRoomJob->errorString());
  });
}

void Controller::createDirectChat(Connection* c, const QString& userID) {
  auto createRoomJob = c->createDirectChat(userID);
  createRoomJob->connect(createRoomJob, &CreateRoomJob::failure, [=] {
    emit errorOccured("Create Direct Chat Failed",
                      createRoomJob->errorString());
  });
}

void Controller::playAudio(QUrl localFile) {
  auto player = new QMediaPlayer;
  player->setMedia(localFile);
  player->play();
  connect(player, &QMediaPlayer::stateChanged, [=] { player->deleteLater(); });
}

void Controller::changeAvatar(Connection* conn, QUrl localFile) {
  auto job = conn->uploadFile(localFile.toLocalFile());
  if (isJobPending(job)) {
    connect(job, &BaseJob::success, this, [conn, job] {
      conn->callApi<SetAvatarUrlJob>(conn->userId(), job->contentUri());
    });
  }
}

void Controller::markAllMessagesAsRead(Connection* conn) {
  for (auto room : conn->allRooms()) {
    room->markAllMessagesAsRead();
  }
}
