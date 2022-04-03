#include <catch2/catch.hpp>

#include <nw/formats/Nss.hpp>
#include <nw/formats/NssAstPrinter.hpp>
#include <nw/formats/NssLexer.hpp>
#include <nw/log.hpp>

#include <iostream>

using namespace nw;

TEST_CASE("NWScript Parser", "[formats]")
{
    Nss nss("test_data/user/development/test.nss");
    auto prog = nss.parse();
    NssAstPrinter p;
    prog.accept(&p);
    LOG_F(INFO, "{}", p.ss.str());
}
