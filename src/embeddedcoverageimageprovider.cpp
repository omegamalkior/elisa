/*
   SPDX-FileCopyrightText: 2018 (c) Matthieu Gallien <matthieu_gallien@yahoo.fr>

   SPDX-License-Identifier: LGPL-3.0-or-later
 */

#include "embeddedcoverageimageprovider.h"

#include <KFileMetaData/EmbeddedImageData>
#include <KFileMetaData/ExtractorCollection>
#include <KFileMetaData/SimpleExtractionResult>

#include "viewsLogging.h"

#include <QCache>
#include <QFileInfo>
#include <QImage>
#include <QMimeDatabase>
#include <QMutex>
#include <QMutexLocker>

namespace
{
// Covers already read from audio files and scaled down for display. Without it,
// every time an album scrolls back into view its audio file is opened again and
// the full size picture is decoded again.
struct CoverCache {
    QMutex mutex;
    QCache<QString, QImage> images{48 * 1024}; // cost is in KiB: at most 48 MiB
};

CoverCache &coverCache()
{
    static CoverCache cache;
    return cache;
}

// a changed file (other date or size) gets another key, so its new cover is read
QString coverCacheKey(const QString &fileName, QSize size)
{
    const QFileInfo fileInfo{fileName};
    return fileName + QLatin1Char('|') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height()) + QLatin1Char('|')
        + QString::number(fileInfo.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') + QString::number(fileInfo.size());
}

class AsyncImageResponse : public QQuickImageResponse, public QRunnable
{
    Q_OBJECT

public:
    AsyncImageResponse(QString id, QSize requestedSize)
        : QQuickImageResponse(), mId(std::move(id)), mRequestedSize(requestedSize)
    {
        setAutoDelete(false);

        if (!mRequestedSize.width()) {
            mRequestedSize.setWidth(mRequestedSize.height());
        }

        if (!mRequestedSize.height()) {
            mRequestedSize.setHeight(mRequestedSize.width());
        }
    }

    [[nodiscard]] QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(mCoverImage);
    }

    void run() override
    {
        const auto cacheKey = coverCacheKey(mId, mRequestedSize);
        {
            QMutexLocker locker(&coverCache().mutex);
            if (const auto *cachedCover = coverCache().images.object(cacheKey)) {
                mCoverImage = *cachedCover;
                qCDebug(orgKdeElisaViews()) << "embedded cover cache hit" << mId;
                locker.unlock();
                Q_EMIT finished();
                return;
            }
        }
        qCDebug(orgKdeElisaViews()) << "embedded cover cache miss" << mId;

        QMimeDatabase mimeDatabase;
        const auto fileMimeType = mimeDatabase.mimeTypeForFile(mId).name();
        KFileMetaData::ExtractorCollection ec;
        KFileMetaData::SimpleExtractionResult result(mId, fileMimeType, KFileMetaData::ExtractionResult::ExtractImageData);

        mErrorMessage = QLatin1String{""};

        const auto extractors = ec.fetchExtractors(fileMimeType);
        for (const auto& ex : extractors) {
            ex->extract(&result);
        }

        auto imageData = result.imageData();

        if (imageData.isEmpty()) {
          mErrorMessage = QString{QLatin1String{"Unable to load image data from "} + mId};
          Q_EMIT finished();
          return;
        }

        if (imageData.contains(KFileMetaData::EmbeddedImageData::FrontCover)) {
          mCoverImage = QImage::fromData(imageData[KFileMetaData::EmbeddedImageData::FrontCover]);
        } else {
          mCoverImage = QImage::fromData(imageData.first());
        }

        if (mCoverImage.isNull()) {
          mErrorMessage = QString{QLatin1String{"Invalid embedded cover image in "} + mId};
          Q_EMIT finished();
          return;
        }

        auto newCoverImage = mCoverImage.scaled(mRequestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!newCoverImage.isNull()) {
          mCoverImage = std::move(newCoverImage);
        }

        // keep display sized covers only (a cover that could not be scaled stays out)
        const auto cost = int(mCoverImage.sizeInBytes() / 1024) + 1;
        if (cost <= 4 * 1024) {
            QMutexLocker locker(&coverCache().mutex);
            coverCache().images.insert(cacheKey, new QImage(mCoverImage), cost);
        }

        Q_EMIT finished();
    }

    QString errorString() const override
    {
      return mErrorMessage;
    }

    QString mId;
    QString mErrorMessage;
    QSize mRequestedSize;
    QImage mCoverImage;
};
}

EmbeddedCoverageImageProvider::EmbeddedCoverageImageProvider()
    : QQuickAsyncImageProvider()
{
}

QQuickImageResponse *EmbeddedCoverageImageProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    const QString decodedId = QUrl::fromPercentEncoding(id.toUtf8());
    auto response = std::make_unique<AsyncImageResponse>(decodedId, requestedSize);
    pool.start(response.get());
    return response.release();
}

#include "embeddedcoverageimageprovider.moc"
