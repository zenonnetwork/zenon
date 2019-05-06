// Copyright (c) 2018 The COLX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ziputil.h"
#include "utiltime.h"
#include "tinyformat.h"

#include <string>
#include <fstream>
#include <boost/test/unit_test.hpp>
#include <boost/filesystem/operations.hpp>

using namespace std;
using namespace boost;

static bool CompareFiles(const string& left, const string& right)
{
    std::ifstream f1(left), f2(right);
    std::string str1((std::istreambuf_iterator<char>(f1)), std::istreambuf_iterator<char>());
    std::string str2((std::istreambuf_iterator<char>(f2)), std::istreambuf_iterator<char>());
    return str1 == str2;
}

BOOST_AUTO_TEST_SUITE(minizip_tests)


BOOST_AUTO_TEST_CASE(zip_unzip_test)
{
    string err;
    const string datadir("data");
    const string unzipdir("data_unzip");
    const string zipfile("data.zip");

    BOOST_REQUIRE(filesystem::exists(datadir));

    // write protected path
    BOOST_CHECK_MESSAGE(!ZipCreate("/root/data.zip", datadir, err), err);

    // invalid data dir
    BOOST_CHECK_MESSAGE(!ZipCreate(zipfile, "not-exist-dir", err), err);

    // create zip
    BOOST_REQUIRE_MESSAGE(ZipCreate(zipfile, datadir, err), err);
    time_t time1 = filesystem::last_write_time(zipfile);
    MilliSleep(1000); // time1 resolution is seconds, so wait to get enough difference

    // overwrite zip
    BOOST_REQUIRE_MESSAGE(ZipCreate(zipfile, datadir, err), err);
    time_t time2 = filesystem::last_write_time(zipfile);

    BOOST_CHECK(time2 > time1);

    if (filesystem::exists(unzipdir))
        filesystem::remove_all(unzipdir);
    filesystem::create_directory(unzipdir);

    const string fOriginal = strprintf("%s/README.md", datadir);
    const string fUnzipped = strprintf("%s/%s/README.md", unzipdir, datadir);

    // extract
    BOOST_REQUIRE_MESSAGE(ZipExtract(zipfile, unzipdir, err), err);
    BOOST_REQUIRE_MESSAGE(CompareFiles(fOriginal, fUnzipped), "files are not equal");

    // modify unzipped file
    ofstream readme(fUnzipped, std::ios_base::app);
    readme << "some content";
    readme.close();
    BOOST_REQUIRE(!CompareFiles(fOriginal, fUnzipped));

    // extract overwrite
    BOOST_REQUIRE_MESSAGE(ZipExtract(zipfile, unzipdir, err), err);
    BOOST_REQUIRE_MESSAGE(CompareFiles(fOriginal, fUnzipped), "files are not equal");

    // invalid input file
    BOOST_CHECK_MESSAGE(!ZipExtract("not-exist-file", unzipdir, err), err);

    // invalid output dir
    BOOST_CHECK_MESSAGE(!ZipExtract(zipfile, "not-exist-dir", err), err);

    // write protected output dir
    BOOST_CHECK_MESSAGE(!ZipExtract(zipfile, "/root", err), err);

    filesystem::remove(zipfile);
    filesystem::remove_all(unzipdir);
}

BOOST_AUTO_TEST_SUITE_END()
