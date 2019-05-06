// Copyright (c) 2018 The COLX developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "context.h"
#include "autoupdatemodel.h"

#include <exception>
#include <boost/test/unit_test.hpp>

using namespace std;

BOOST_AUTO_TEST_SUITE(context_tests)


BOOST_AUTO_TEST_CASE(context_init_test)
{
    BOOST_CHECK_THROW(GetContext(), runtime_error);

    CreateContext();
    BOOST_CHECK_THROW(CreateContext(), runtime_error);

    BOOST_CHECK(GetContext().GetAutoUpdateModel()->IsUpdateAvailable() == false);

    GetContext().GetAutoUpdateModel()->SetUpdateAvailable(true, "url", "filename");
    BOOST_CHECK(GetContext().GetAutoUpdateModel()->IsUpdateAvailable() == true);
    BOOST_CHECK(GetContext().GetAutoUpdateModel()->GetUpdateUrlFile() == "filename");
    BOOST_CHECK(GetContext().GetAutoUpdateModel()->GetUpdateUrlTag() == "url");

    ReleaseContext();
    BOOST_CHECK_THROW(GetContext(), runtime_error);
}


BOOST_AUTO_TEST_SUITE_END()
