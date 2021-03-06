
// QT headers
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QApplication>

// MythTV headers
#include <mythcontext.h>
#include <mythdbcon.h>
#include <mythdb.h>
#include <mythprogressdialog.h>
#include <mythdirs.h>

// MythWeather headers
#include "weatherScreen.h"
#include "weatherSource.h"
#include "sourceManager.h"

#define LOC QString("SourceManager: ")
#define LOC_ERR QString("SourceManager Error: ")

SourceManager::SourceManager()
{
    findScriptsDB();
    setupSources();
}

SourceManager::~SourceManager()
{
    clearSources();
}

bool SourceManager::findScriptsDB()
{
    MSqlQuery db(MSqlQuery::InitCon());
    QString query =
            "SELECT DISTINCT wss.sourceid, source_name, update_timeout, "
            "retrieve_timeout, path, author, version, email, types "
            "FROM weathersourcesettings wss "
            "LEFT JOIN weatherdatalayout wdl "
            "ON wss.sourceid = wdl.weathersourcesettings_sourceid "
            "WHERE hostname = :HOST;";

    db.prepare(query);
    db.bindValue(":HOST", gCoreContext->GetHostName());
    if (!db.exec())
    {
        MythDB::DBError("Finding weather source scripts for host", db);
        return false;
    }

    while (db.next())
    {
        QFileInfo fi(db.value(4).toString());

        if (!fi.isExecutable())
        {
            // scripts will be deleted from db in the more robust (i.e. slower)
            // findScripts() -- run when entering setup
            continue;
        }
        ScriptInfo *si = new ScriptInfo;
        si->id = db.value(0).toInt();
        si->name = db.value(1).toString();
        si->updateTimeout = db.value(2).toUInt() * 1000;
        si->scriptTimeout = db.value(3).toUInt() * 1000;
        si->fileInfo = fi;
        si->author = db.value(5).toString();
        si->version = db.value(6).toString();
        si->email = db.value(7).toString();
        si->types = db.value(8).toString().split(",");
        m_scripts.append(si);
    }

    return true;
}

bool SourceManager::findScripts()
{
    QString path = GetShareDir() + "mythweather/scripts/";
    QDir dir(path);
    dir.setFilter(QDir::Executable | QDir::Files | QDir::Dirs);

    if (!dir.exists())
    {
        VERBOSE(VB_IMPORTANT, "MythWeather: Scripts directory not found");
        return false;
    }
    QString busymessage = tr("Searching for scripts");

    MythScreenStack *popupStack = GetMythMainWindow()->GetStack("weather stack");
    if (popupStack == NULL)
        popupStack = GetMythMainWindow()->GetStack("popup stack");

    MythUIBusyDialog *busyPopup = new MythUIBusyDialog(busymessage, popupStack,
                                                       "mythweatherbusydialog");

    if (busyPopup->Create())
    {
        popupStack->AddScreen(busyPopup, false);
    }
    else
    {
        delete busyPopup;
        busyPopup = NULL;
    }

    qApp->processEvents();

    recurseDirs(dir);

    // run through and see if any scripts have been deleted
    MSqlQuery db(MSqlQuery::InitCon());

    db.prepare("SELECT sourceid, path FROM weathersourcesettings "
               "WHERE hostname = :HOST;");
    db.bindValue(":HOST", gCoreContext->GetHostName());
    if (!db.exec())
        MythDB::DBError("SourceManager::findScripts - select", db);
    QStringList toRemove;
    while (db.next())
    {
        QFileInfo fi(db.value(1).toString());
        if (!fi.isExecutable())
        {
            toRemove << db.value(0).toString();
            VERBOSE(VB_IMPORTANT,
                    QString("'%1' no longer exists")
                    .arg(fi.absoluteFilePath()));
        }
    }

    db.prepare("DELETE FROM weathersourcesettings WHERE sourceid = :ID;");
    for (int i = 0; i < toRemove.count(); ++i)
    {
        db.bindValue(":ID", toRemove[i]);
        if (!db.exec())
        {
            MythDB::DBError("Deleting weather source settings", db);
        }
    }

    if (busyPopup)
    {
        busyPopup->Close();
        busyPopup = NULL;
    }

    return m_scripts.count() > 0;
}

void SourceManager::clearSources()
{
    while (!m_scripts.isEmpty())
        delete m_scripts.takeFirst();
    m_scripts.clear();

    while (!m_sources.isEmpty())
        delete m_sources.takeFirst();
    m_sources.clear();
}

void SourceManager::setupSources()
{
    MSqlQuery db(MSqlQuery::InitCon());

    db.prepare(
        "SELECT DISTINCT location, weathersourcesettings_sourceid, "
        "                weatherscreens.units, weatherscreens.screen_id "
        "FROM weatherdatalayout,weatherscreens "
        "WHERE weatherscreens.screen_id = weatherscreens_screen_id AND "
        "      weatherscreens.hostname = :HOST");
    db.bindValue(":HOST", gCoreContext->GetHostName());
    if (!db.exec())
    {
        MythDB::DBError("Finding weather sources for this host", db);
        return;
    }

    m_sourcemap.clear();

    while (db.next())
    {
        QString loc = db.value(0).toString();
        uint sourceid = db.value(1).toUInt();
        units_t units = db.value(2).toUInt();
        uint screen = db.value(3).toUInt();
        const WeatherSource *src = needSourceFor(sourceid, loc, units);
        if (src)
            m_sourcemap.insert((long)screen, src);
    }
}

ScriptInfo *SourceManager::getSourceByName(const QString &name)
{
    ScriptInfo *src = 0;
    for (int x = 0; x < m_scripts.size(); x++)
    {
        src = m_scripts.at(x);
        if (src->name == name)
        {
            return src;
          //  src = new ScriptInfo;
          //  src->name = m_scripts[i]->name;
          //  src->version = m_scripts[i]->version;
          //  src->author = m_scripts[i]->author;
          //  src->email = m_scripts[i]->email;
          //  src->types = m_scripts[i]->types;
          //  src->file = new QFileInfo(*m_scripts[i]->file);
          //  src->scriptTimeout = m_scripts[i]->scriptTimeout;
          //  src->updateTimeout = m_scripts[i]->updateTimeout;
          //  src->id = m_scripts[i]->id;
        }
    }

    if (!src)
    {
        VERBOSE(VB_IMPORTANT, "No Source found for " + name);
    }

    return NULL;
}

QStringList SourceManager::getLocationList(ScriptInfo *si, const QString &str)
{
    if (!m_scripts.contains(si))
        return QStringList();
    WeatherSource *ws = new WeatherSource(si);

    QStringList locationList(ws->getLocationList(str));

    delete ws;

    return locationList;
}

WeatherSource *SourceManager::needSourceFor(int id, const QString &loc,
                                            units_t units)
{
    // matching source exists?
    WeatherSource *src;
    for (int x = 0; x < m_sources.size(); x++)
    {
        src = m_sources.at(x);
        if (src->getId() == id && src->getLocale() == loc &&
            src->getUnits() == units)
        {
            return src;
        }
    }

    // no matching source, make one
    ScriptInfo *si;
    for (int x = 0; x < m_scripts.size(); x++)
    {
        si = m_scripts.at(x);
        if (si->id == id)
        {
            WeatherSource *ws = new WeatherSource(si);
            ws->setLocale(loc);
            ws->setUnits(units);
            m_sources.append(ws);
            return ws;
        }
    }

    VERBOSE(VB_IMPORTANT, LOC + QString("NeedSourceFor: Unable to find source "
            "for %1, %2, %3").arg(id).arg(loc).arg(units));
    return NULL;
}

void SourceManager::startTimers()
{
    WeatherSource *src;
    for (int x = 0; x < m_sources.size(); x++)
    {
        src = m_sources.at(x);
        src->startUpdateTimer();
    }
}

void SourceManager::stopTimers()
{
    WeatherSource *src;
    for (int x = 0; x < m_sources.size(); x++)
    {
        src = m_sources.at(x);
        src->stopUpdateTimer();
    }
}

void SourceManager::doUpdate(bool forceUpdate)
{
    WeatherSource *src;
    for (int x = 0; x < m_sources.size(); x++)
    {
        src = m_sources.at(x);
        if (src->isRunning())
        {
            VERBOSE(VB_GENERAL, tr("Script %1 is still running when trying to do update, "
                    "Make sure it isn't hanging, make sure timeout values are sane... "
                    "Not running this time around").arg(src->getName()));
        }
        else if (src->inUse())
            src->startUpdate(forceUpdate);
    }
}

bool SourceManager::findPossibleSources(QStringList types,
                                        QList<ScriptInfo *> &sources)
{
    ScriptInfo *si;
    bool handled;
    for (int x = 0; x < m_scripts.size(); x++)
    {
        si = m_scripts.at(x);
        QStringList stypes = si->types;
        handled = true;
        int i;
        for (i = 0; i < types.count() && handled; ++i)
        {
            handled = stypes.contains(types[i]);
        }
        if (handled)
            sources.append(si);
    }

    if (sources.count())
        return true;

    return false;
}

bool SourceManager::connectScreen(uint id, WeatherScreen *screen)
{
    if (!screen)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Cannot connect nonexistent screen "<<screen);

        return false;
    }

    SourceMap::iterator it = m_sourcemap.find(id);
    if (it == m_sourcemap.end())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                QString("Cannot connect nonexistent source '%1'").arg(id));

        return false;
    }

    (const_cast<WeatherSource*>(*it))->connectScreen(screen);

    return true;
}

bool SourceManager::disconnectScreen(WeatherScreen *screen)
{
    if (!screen)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Cannot disconnect nonexistent screen "<<screen);

        return false;
    }

    SourceMap::iterator it = m_sourcemap.find(screen->getId());
    if (it == m_sourcemap.end())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Cannot disconnect nonexistent source "<<screen->getId());

        return false;
    }

    (const_cast<WeatherSource*>(*it))->disconnectScreen(screen);

    return true;
}

// Recurses dir for script files
void SourceManager::recurseDirs( QDir dir )
{
    if (!dir.exists())
        return;

    dir.setFilter(QDir::Executable | QDir::Files | QDir::Dirs);
    QFileInfoList files = dir.entryInfoList();
    QFileInfo file;

    for (int x = 0; x < files.size(); x++)
    {
        qApp->processEvents();
        file = files.at(x);
        if (file.isDir())
        {
            if (file.fileName() == QString("..")) continue;
            if (file.fileName() == QString("."))  continue;
            QDir recurseTo(file.filePath());
            recurseDirs(recurseTo);
        }

        if (file.isExecutable() && !(file.isDir()))
        {
            ScriptInfo *info = WeatherSource::ProbeScript(file);
            if (info)
            {
                m_scripts.append(info);
                VERBOSE(VB_FILE,
                        QString("Found Script '%1'")
                        .arg(file.absoluteFilePath()));
            }
        }
    }

    return;
}
