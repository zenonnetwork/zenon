// Copyright (c) 2018 The COLX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "curl.h"

#include <string>
#include <fstream>
#include <boost/test/unit_test.hpp>
#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost;

BOOST_AUTO_TEST_SUITE(curl_tests)


BOOST_AUTO_TEST_CASE(curl_getredirect_test)
{
    string err1, out1;
    BOOST_CHECK(CURLGetRedirect("https://github.com/zenonnetwork/zenon/releases/latest", out1, err1));
    BOOST_CHECK_MESSAGE(err1.empty(), err1);
    BOOST_CHECK(!out1.empty());

    string err2, out2;
    BOOST_CHECK(!CURLGetRedirect("http://icanhazip.com", out2, err2));
    BOOST_CHECK(!err2.empty());
    BOOST_CHECK_MESSAGE(out2.empty(), out2);
}

BOOST_AUTO_TEST_CASE(curl_download_test)
{
    const CUrl url = "https://github.com/zenonnetwork/zenon/releases/download/1.0.0-alpha/Zenon-1.0.0-linux64.zip";
    const string tmpFile("download.tmp");
    auto ProgressFn = [](double total, double now) { return 0; };
    string buff, err;

    {// test with progress fn
        BOOST_REQUIRE_MESSAGE(CURLDownloadToMem(url, ProgressFn, buff, err), err);
        BOOST_CHECK_MESSAGE(err.empty(), err);
        BOOST_REQUIRE_MESSAGE(CURLDownloadToFile(url, tmpFile, ProgressFn, err), err);
        BOOST_CHECK_MESSAGE(err.empty(), err);

        std::ifstream f(tmpFile);
        std::string filebuff((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        BOOST_CHECK(buff == filebuff);

        filesystem::remove(tmpFile);
    }

    {// test without progress fn
        BOOST_REQUIRE_MESSAGE(CURLDownloadToMem(url, nullptr, buff, err), err);
        BOOST_CHECK_MESSAGE(err.empty(), err);
        BOOST_REQUIRE_MESSAGE(CURLDownloadToFile(url, tmpFile, nullptr, err), err);
        BOOST_CHECK_MESSAGE(err.empty(), err);

        std::ifstream f(tmpFile);
        std::string filebuff((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        BOOST_CHECK(buff == filebuff);

        filesystem::remove(tmpFile);
    }

    // test empty url
    BOOST_CHECK(!CURLDownloadToMem("", nullptr, buff, err));
    BOOST_CHECK(!err.empty());
    BOOST_CHECK(!CURLDownloadToFile("", tmpFile, nullptr, err));
    BOOST_CHECK(!err.empty());

    // test empty file
    BOOST_CHECK(!CURLDownloadToFile(url, "", nullptr, err));
    BOOST_CHECK(!err.empty());

    // test no permissions
    BOOST_CHECK(!CURLDownloadToFile(url, "/root/file.tmp", nullptr, err));
    BOOST_CHECK(!err.empty());

    // test invalid url
    BOOST_CHECK(!CURLDownloadToMem("invalid_url", nullptr, buff, err));
    BOOST_CHECK(!err.empty());
    BOOST_CHECK(!CURLDownloadToFile("invalid_url", tmpFile, nullptr, err));
    BOOST_CHECK(!err.empty());

    filesystem::remove(tmpFile);
}

BOOST_AUTO_TEST_SUITE_END()
