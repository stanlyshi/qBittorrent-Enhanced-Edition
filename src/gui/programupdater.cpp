/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2021  Mike Tzou (Chocobo1)
 * Copyright (C) 2010  Christophe Dumez <chris@qbittorrent.org>
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
 */

#include "programupdater.h"

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <versionhelpers.h>  // must follow after Windows.h
#endif

#include <QDebug>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QXmlStreamReader>

#if defined(Q_OS_WIN)
#include <QSysInfo>
#endif

#include "base/net/downloadmanager.h"
#include "base/utils/version.h"
#include "base/version.h"

namespace
{
    bool isVersionMoreRecent(const QString &remoteVersion)
    {
        using Version = Utils::Version<int, 4, 3>;

        try
        {
            const Version newVersion {remoteVersion};
            const Version currentVersion {QBT_VERSION_MAJOR, QBT_VERSION_MINOR, QBT_VERSION_BUGFIX, QBT_VERSION_BUILD};
            if (newVersion == currentVersion)
            {
                const bool isDevVersion = QString::fromLatin1(QBT_VERSION_STATUS).contains(
                    QRegularExpression(QLatin1String("(alpha|beta|rc)")));
                if (isDevVersion)
                    return true;
            }
            return (newVersion > currentVersion);
        }
        catch (const RuntimeError &)
        {
            return false;
        }
    }
}

void ProgramUpdater::checkForUpdates() const
{
    const auto RSS_URL = QString::fromLatin1("https://husky.moe/feedqBittorent.xml");
    // Don't change this User-Agent. In case our updater goes haywire,
    // the filehost can identify it and contact us.
    Net::DownloadManager::instance()->download(
        Net::DownloadRequest(RSS_URL).userAgent("qBittorrent Enhanced/" QBT_VERSION_2 " ProgramUpdater (git.io/qbit)")
        , this, &ProgramUpdater::rssDownloadFinished);
}

QString ProgramUpdater::getNewVersion() const
{
    return m_newVersion;
}

QString ProgramUpdater::getNewContent() const
{
  return m_content;
}

QString ProgramUpdater::getNextUpdate() const
{
  return m_nextUpdate;
}

void ProgramUpdater::rssDownloadFinished(const Net::DownloadResult &result)
{
    if (result.status != Net::DownloadStatus::Success)
    {
        qDebug() << "Downloading the new qBittorrent updates RSS failed:" << result.errorString;
        emit updateCheckFinished();
        return;
    }

    qDebug("Finished downloading the new qBittorrent updates RSS");

    const auto getStringValue = [](QXmlStreamReader &xml) -> QString
    {
        xml.readNext();
        return (xml.isCharacters() && !xml.isWhitespace())
            ? xml.text().toString()
            : QString {};
    };

#ifdef Q_OS_MACOS
    const QString OS_TYPE {"Mac OS X"};
#elif defined(Q_OS_WIN)
    const QString OS_TYPE {(::IsWindows7OrGreater()
        && QSysInfo::currentCpuArchitecture().endsWith("64"))
        ? "Windows x64" : "Windows"};
#endif

    bool inItem = false;
    QString version;
    QString content;
    QString nextUpdate;
    QString updateLink;
    QString type;
    QXmlStreamReader xml(result.data);

    while (!xml.atEnd())
    {
        xml.readNext();

        if (xml.isStartElement())
        {
            if (xml.name() == QLatin1String("item"))
                inItem = true;
            else if (inItem && (xml.name() == QLatin1String("link")))
                updateLink = getStringValue(xml);
            else if (inItem && (xml.name() == QLatin1String("type")))
                type = getStringValue(xml);
            else if (inItem && (xml.name() == QLatin1String("version")))
                version = getStringValue(xml);
            else if (inItem && (xml.name() == QLatin1String("content")))
                content = getStringValue(xml);
            else if (inItem && (xml.name() == QLatin1String("update")))
                nextUpdate = getStringValue(xml);
        }
        else if (xml.isEndElement())
        {
            if (inItem && (xml.name() == QLatin1String("item")))
            {
                if (type.compare(OS_TYPE, Qt::CaseInsensitive) == 0)
                {
                    qDebug("The last update available is %s", qUtf8Printable(version));
                    if (!version.isEmpty())
                    {
                        qDebug("Detected version is %s", qUtf8Printable(version));
                        if (isVersionMoreRecent(version))
                        {
                            m_newVersion = version;
                            m_updateURL = updateLink;
                            m_content = content;
                        }
                        m_nextUpdate = nextUpdate;
                    }
                    break;
                }

                inItem = false;
                updateLink.clear();
                type.clear();
                version.clear();
                content.clear();
                nextUpdate.clear();
            }
        }
    }

    emit updateCheckFinished();
}

bool ProgramUpdater::updateProgram() const
{
    return QDesktopServices::openUrl(m_updateURL);
}
