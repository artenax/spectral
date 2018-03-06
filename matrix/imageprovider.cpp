#include "imageprovider.h"

#include "connection.h"
#include "jobs/mediathumbnailjob.h"

#include <QtCore/QWaitCondition>
#include <QtCore/QDebug>
#include <QMetaObject>

using QMatrixClient::MediaThumbnailJob;

ImageProvider::ImageProvider(QObject *parent)
    : QQuickImageProvider(QQmlImageProviderBase::Image,
                          QQmlImageProviderBase::ForceAsynchronousImageLoading)
{
#if (QT_VERSION < QT_VERSION_CHECK(5, 10, 0))
    qRegisterMetaType<MediaThumbnailJob*>();
#endif
}

QImage ImageProvider::requestImage(const QString& id,
                                   QSize* pSize, const QSize& requestedSize)
{
    if (!id.startsWith("mxc://"))
    {
        qWarning() << "ImageProvider: won't fetch an invalid id:" << id
                   << "doesn't follow server/mediaId pattern";
        return {};
    }

    QUrl mxcUri { id };
    qDebug() << "ImageProvider::requestImage:" << mxcUri.toString();

    MediaThumbnailJob* job = nullptr;
    QReadLocker locker(&m_lock);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 10, 0))
    QMetaObject::invokeMethod(m_connection,
        [=] { return m_connection->getThumbnail(mxcUri, requestedSize); },
        Qt::BlockingQueuedConnection, &job);
#else
    QMetaObject::invokeMethod(m_connection, "getThumbnail",
        Qt::BlockingQueuedConnection, Q_RETURN_ARG(MediaThumbnailJob*, job),
        Q_ARG(QUrl, mxcUri), Q_ARG(QSize, requestedSize));
#endif
    if (!job)
    {
        qDebug() << "ImageProvider: failed to send a request";
        return {};
    }
    QImage result;
    {
        QWaitCondition condition; // The most compact way to block on a signal
        job->connect(job, &MediaThumbnailJob::finished, job, [&] {
            result = job->thumbnail();
            condition.wakeAll();
        });
        condition.wait(&m_lock);
    }

    if(pSize != nullptr)
        *pSize = result.size();

    return result;
}

void ImageProvider::setConnection(QMatrixClient::Connection* connection)
{
    QWriteLocker locker(&m_lock);

    m_connection = connection;
}
