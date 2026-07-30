#include "../tools/smalls-lsp/lsp_uri.hpp"

#include <gtest/gtest.h>

#include <string>

using smalls_lsp::native_path_to_uri;
using smalls_lsp::PathStyle;
using smalls_lsp::uri_to_native_path;

namespace {

/// Asserts that a native path survives a trip through a URI and back.
void expect_round_trip(std::string_view path, PathStyle style)
{
    std::string uri = native_path_to_uri(path, style);
    auto back = uri_to_native_path(uri, style);
    ASSERT_TRUE(back) << "failed to decode " << uri;
    EXPECT_EQ(*back, path) << "via " << uri;
}

} // namespace

// == POSIX ===================================================================

TEST(LspUri, PosixAbsolutePath)
{
    EXPECT_EQ(native_path_to_uri("/tmp/a.smalls", PathStyle::posix),
        "file:///tmp/a.smalls");
    auto path = uri_to_native_path("file:///tmp/a.smalls", PathStyle::posix);
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "/tmp/a.smalls");
}

TEST(LspUri, PosixRoundTripsAwkwardBytes)
{
    expect_round_trip("/tmp/with space/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/hash#name/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/percent%20literal/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/héllo/wörld.smalls", PathStyle::posix);
    // UTF-8 bytes rather than a UCN, which MSVC cannot encode in CP-1252.
    expect_round_trip("/tmp/emoji\xF0\x9F\x98\x80/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/quote'and\"quote/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/plus+amp&/a.smalls", PathStyle::posix);
    expect_round_trip("/tmp/colon:name/a.smalls", PathStyle::posix);
}

// A literal `#` starts a fragment, so a file name containing one must be
// encoded. Encoding it is what makes the round trip above work.
TEST(LspUri, PosixEncodesHashAndSpace)
{
    EXPECT_EQ(native_path_to_uri("/a b#c", PathStyle::posix), "file:///a%20b%23c");
}

TEST(LspUri, PosixFragmentAndQueryTerminateThePath)
{
    auto fragment = uri_to_native_path("file:///tmp/a.smalls#L12", PathStyle::posix);
    ASSERT_TRUE(fragment);
    EXPECT_EQ(*fragment, "/tmp/a.smalls");

    auto query = uri_to_native_path("file:///tmp/a.smalls?v=2", PathStyle::posix);
    ASSERT_TRUE(query);
    EXPECT_EQ(*query, "/tmp/a.smalls");
}

TEST(LspUri, PosixAcceptsMinimalAndLocalhostForms)
{
    auto minimal = uri_to_native_path("file:/tmp/a.smalls", PathStyle::posix);
    ASSERT_TRUE(minimal);
    EXPECT_EQ(*minimal, "/tmp/a.smalls");

    auto localhost = uri_to_native_path("file://localhost/tmp/a.smalls", PathStyle::posix);
    ASSERT_TRUE(localhost);
    EXPECT_EQ(*localhost, "/tmp/a.smalls");
}

TEST(LspUri, PosixRejectsRemoteAuthority)
{
    // POSIX has no spelling for a remote host, so this must not be guessed at.
    EXPECT_FALSE(uri_to_native_path("file://server/share/a.smalls", PathStyle::posix));
}

// == Windows =================================================================

TEST(LspUri, WindowsDrivePath)
{
    auto path = uri_to_native_path("file:///c%3A/Users/a.smalls", PathStyle::windows);
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "c:\\Users\\a.smalls");

    // VS Code also emits the unencoded colon form.
    auto plain = uri_to_native_path("file:///C:/Users/a.smalls", PathStyle::windows);
    ASSERT_TRUE(plain);
    EXPECT_EQ(*plain, "C:\\Users\\a.smalls");
}

TEST(LspUri, WindowsAcceptsLegacyBarDriveForm)
{
    auto path = uri_to_native_path("file:///C|/Users/a.smalls", PathStyle::windows);
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "C:\\Users\\a.smalls");
}

TEST(LspUri, WindowsEncodesDriveColon)
{
    EXPECT_EQ(native_path_to_uri("C:\\Users\\a.smalls", PathStyle::windows),
        "file:///C%3A/Users/a.smalls");
}

TEST(LspUri, WindowsRoundTripsDriveAndAwkwardBytes)
{
    expect_round_trip("C:\\Users\\a.smalls", PathStyle::windows);
    expect_round_trip("C:\\Program Files\\smalls\\a.smalls", PathStyle::windows);
    expect_round_trip("C:\\hash#name\\a.smalls", PathStyle::windows);
    expect_round_trip("C:\\héllo\\wörld.smalls", PathStyle::windows);
    expect_round_trip("D:\\percent%20literal\\a.smalls", PathStyle::windows);
}

TEST(LspUri, WindowsUncAuthority)
{
    auto path = uri_to_native_path("file://server/share/a.smalls", PathStyle::windows);
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "\\\\server\\share\\a.smalls");

    EXPECT_EQ(native_path_to_uri("\\\\server\\share\\a.smalls", PathStyle::windows),
        "file://server/share/a.smalls");
}

TEST(LspUri, WindowsRoundTripsUnc)
{
    expect_round_trip("\\\\server\\share\\a.smalls", PathStyle::windows);
    expect_round_trip("\\\\server\\share with space\\a.smalls", PathStyle::windows);
}

TEST(LspUri, WindowsLocalhostAuthorityIsLocal)
{
    auto path = uri_to_native_path("file://localhost/C:/a.smalls", PathStyle::windows);
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "C:\\a.smalls");
}

// == Rejection ===============================================================

TEST(LspUri, RejectsNonFileSchemes)
{
    for (const char* uri : {"http://example.com/a.smalls", "untitled:Untitled-1",
             "vscode-vfs://host/a.smalls", "/tmp/not-a-uri.smalls", ""}) {
        EXPECT_FALSE(uri_to_native_path(uri, PathStyle::posix)) << uri;
    }
}

TEST(LspUri, AcceptsSchemeCaseInsensitively)
{
    EXPECT_TRUE(uri_to_native_path("FILE:///tmp/a.smalls", PathStyle::posix));
    EXPECT_TRUE(uri_to_native_path("File:///tmp/a.smalls", PathStyle::posix));
}

TEST(LspUri, RejectsInvalidPercentEscapes)
{
    // A malformed escape must fail rather than pass the '%' through, or a
    // decoded path silently differs from the one the client meant.
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%.smalls", PathStyle::posix));
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%2.smalls", PathStyle::posix));
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%zz.smalls", PathStyle::posix));
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%2", PathStyle::posix));
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%", PathStyle::posix));
}

TEST(LspUri, RejectsEncodedNul)
{
    EXPECT_FALSE(uri_to_native_path("file:///tmp/a%00b", PathStyle::posix));
}

TEST(LspUri, RejectsRelativeReference)
{
    EXPECT_FALSE(uri_to_native_path("file:relative/a.smalls", PathStyle::posix));
}

TEST(LspUri, RejectsEmptyPath)
{
    EXPECT_FALSE(uri_to_native_path("file://", PathStyle::posix));
    EXPECT_FALSE(uri_to_native_path("file://localhost", PathStyle::posix));
}
