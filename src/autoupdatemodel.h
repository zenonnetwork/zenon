// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2015-2018 The COLX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AUTOUPDATEMODEL_H
#define BITCOIN_AUTOUPDATEMODEL_H

#include <string>
#include <memory>
#include <atomic>

#include <boost/thread/thread.hpp>
#include <boost/filesystem/path.hpp>

#include "sync.h"

/**
 * @brief The AutoUpdateModel class is responsible for querying
 * github releases for new release availability and notifying user about it.
 */
class AutoUpdateModel
{
public:
    ~AutoUpdateModel();

    /**
     * Set availability of the update on the server.
     * @param available true - available, false - not available
     * @param urlTag base url of the release folder on the server
     * @param urlFile url of the release file on the server
     */
    void SetUpdateAvailable(bool available, const std::string& urlTag, const std::string& urlFile);

    /**
     * Return availability of the update on the server.
     * @return true - available, false - not available
     */
    bool IsUpdateAvailable() const;

    /**
     * Return full URL of the release folder on the server.
     */
    std::string GetUpdateUrlTag() const;

    /**
     * Return full URL of the release file on the server.
     */
    std::string GetUpdateUrlFile() const;

    /**
     * @brief Run download task in the background, return immediately.
     *        In order to track download progress connect to the CClientUIInterface::NotifyUpdateDownloadProgress,
     *        nProgress > 100 when download has completed.
     *
     * @param err error description on fail
     * @return true - success, false - failed
     */
    bool DownloadUpdateUrlFile(std::string& err);

    /**
     * Return true if download task is running, false - otherwise.
     */
    bool IsDownloadRunning();

    /**
     * @brief Return status of the latest async operation DownloadUpdateUrlFile.
     *        If operation is in progress - wait for completion!!!
     *
     * @param err error description on fail
     * @return true - success, false - failed
     */
    bool IsLatestRunSuccess(std::string& err) const;

    /**
     * Return full path to the local file, if not exists - return empty string.
     */
    std::string FindLocalFile() const;

private:
    AutoUpdateModel();
    AutoUpdateModel(const AutoUpdateModel&);
    AutoUpdateModel& operator=(const AutoUpdateModel&);
    friend class CContext;

    void ResetState();
    void Wait() const;
    void Cancel();

    void DownloadUpdateUrlFileThread();
    bool DownloadUpdateUrlFileImpl(std::string& err);
    std::string BuildLocalFilePath(const std::string& url) const;

private:
    bool bUpdateAvailable_ = false;
    std::string sUpdateUrlTag_;                     /** URL of the remote folder */
    std::string sUpdateUrlFile_;                    /** URL of the remote file */
    std::unique_ptr<CCriticalSection> csUpdate_;
    boost::filesystem::path datadirPath_;           /** network specific data dir */

    std::atomic<int> progress_;
    std::atomic<bool> cancel_;
    std::string latestRunError_;
    std::unique_ptr<boost::thread> workerThread_;
};

#endif // BITCOIN_AUTOUPDATEMODEL_H
