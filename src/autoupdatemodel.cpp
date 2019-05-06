// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2015-2018 The COLX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "autoupdatemodel.h"
#include "ui_interface.h"
#include "tinyformat.h"
#include "curl.h"
#include "util.h"

#include <chrono>
#include <boost/exception/all.hpp>
#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost;
using namespace boost::filesystem;

static const char UPDATE_FOLDER[] = "update";
static const int DOWNLOAD_THREAD_EXIT = 101;

AutoUpdateModel::AutoUpdateModel():
    csUpdate_(new CCriticalSection),
    datadirPath_(GetDataDir())
{
    if (!exists(datadirPath_))
        throw logic_error(strprintf("%s: data dir does not exist: %s", __func__, datadirPath_.string()));
}

AutoUpdateModel::~AutoUpdateModel()
{
    assert(!IsDownloadRunning()); // better to cancel download in the logic
    Cancel();
    Wait();
}

void AutoUpdateModel::SetUpdateAvailable(bool available, const string& urlTag, const string& urlFile)
{
    LOCK(*csUpdate_);

    if (available && (urlTag.empty() || urlFile.empty())) {
        assert(false);
        return;
    }

    bUpdateAvailable_ = available;
    sUpdateUrlTag_ = urlTag;
    sUpdateUrlFile_ = urlFile;
}

bool AutoUpdateModel::IsUpdateAvailable() const
{
    LOCK(*csUpdate_);
    return bUpdateAvailable_;
}

std::string AutoUpdateModel::GetUpdateUrlTag() const
{
    LOCK(*csUpdate_);
    return sUpdateUrlTag_;
}

std::string AutoUpdateModel::GetUpdateUrlFile() const
{
    LOCK(*csUpdate_);
    return sUpdateUrlFile_;
}

bool AutoUpdateModel::DownloadUpdateUrlFile(string& err)
{
    try {
        if (IsDownloadRunning()) {
            err = "Download in progress";
            return false;
        } else if (GetUpdateUrlFile().empty()) {
            err = "Remote URL is missing";
            return false;
        } else {
            workerThread_.reset(new boost::thread([this](){ DownloadUpdateUrlFileThread(); }));
            return true;
        }
    } catch (const boost::exception& e) {
        err = boost::diagnostic_information(e);
    } catch (const std::exception& e) {
        err = e.what();
    } catch (...) {
        err = "unexpected error";
    }

    return error("%s : %s", __func__, err);
}

void AutoUpdateModel::DownloadUpdateUrlFileThread()
{
    string thName = strprintf("Zenon-%s", __func__);
    RenameThread(thName.c_str());
    LogPrintf("%s thread start\n", thName);

    try {
        ResetState();
        DownloadUpdateUrlFileImpl(latestRunError_);
    } catch (const std::exception& e) {
        latestRunError_ = e.what();
    } catch (...) {
        latestRunError_ = "unexpected error";
    }

    if (!latestRunError_.empty())
        error("%s : %s", __func__, latestRunError_);

    uiInterface.NotifyUpdateDownloadProgress("", DOWNLOAD_THREAD_EXIT);
    LogPrintf("%s thread exit\n", thName);
}

bool AutoUpdateModel::DownloadUpdateUrlFileImpl(std::string& err)
{
    const string url = GetUpdateUrlFile();
    if (url.empty()) {
        err = "URL must not be empty";
        return error("%s : %s", __func__, err);
    }

    const string localFilePath = BuildLocalFilePath(url);
    if (localFilePath.empty()) {
        err = strprintf("Path must not be empty, url is %s", url);
        return error("%s : %s", __func__, err);
    }

    create_directory(datadirPath_ / UPDATE_FOLDER);

    if (exists(localFilePath) && is_regular_file(localFilePath))
        remove(localFilePath);

    const string tmpPath = strprintf("%s.tmp", localFilePath);
    if (exists(tmpPath) && is_regular_file(tmpPath))
        remove(tmpPath);

    int speed = 0, bytes = 0;
    using time_point = std::chrono::system_clock::time_point;
    time_point tp1 = std::chrono::system_clock::now();
    bool success = CURLDownloadToFile(url, tmpPath,
        [this, &speed, &bytes, &tp1](double total, double now)->int{
        if (cancel_)
            return CURL_CANCEL_DOWNLOAD;
        else {
            time_point tp2 = std::chrono::system_clock::now();
            size_t sec = std::chrono::duration_cast<std::chrono::seconds>(tp2 - tp1).count();
            if (sec > 5) {
                speed = (now - bytes) / sec;
                bytes = now;
                tp1 = std::chrono::system_clock::now();
            }

            if (now > 0 && total > 0 && total >= now)
                progress_ = static_cast<int>(100.0 * now / total);

            const string str = strprintf("Downloading %s (%s/s)",
                HumanReadableSize(static_cast<int>(now), false), HumanReadableSize(speed, false));

            uiInterface.NotifyUpdateDownloadProgress(str, progress_);
            return CURL_CONTINUE_DOWNLOAD;
        }
    } , err);

    if (success) {
        rename(tmpPath, localFilePath);
        return true;
    } else {
        remove(tmpPath);
        return error("%s : %s", __func__, err);
    }
}

bool AutoUpdateModel::IsDownloadRunning()
{
    return workerThread_ && workerThread_->joinable() &&
        !workerThread_->try_join_for(boost::chrono::milliseconds(1));
}

bool AutoUpdateModel::IsLatestRunSuccess(std::string& err) const
{
    Wait();
    err = latestRunError_;
    return err.empty();
}

void AutoUpdateModel::Cancel()
{
    cancel_ = true;
}

void AutoUpdateModel::Wait() const
{
    if (workerThread_ && workerThread_->joinable())
        workerThread_->join();
}

void AutoUpdateModel::ResetState()
{
    progress_ = 0;
    cancel_ = false;
    latestRunError_.clear();
}

string AutoUpdateModel::FindLocalFile() const
{
    string remoteFile = GetUpdateUrlFile();
    if (remoteFile.empty())
        return string();

    string localFile = BuildLocalFilePath(remoteFile);
    if (!localFile.empty() && exists(localFile))
        return localFile;
    else
        return string();
}

string AutoUpdateModel::BuildLocalFilePath(const string& url) const
{
    string::size_type pos = url.find_last_of('/');
    if (string::npos == pos)
        return string();
    else {
        string name = url.substr(pos + 1);
        return (datadirPath_ / UPDATE_FOLDER / name).string();
    }
}
