#pragma once

#include "../kernel/Strings.hpp"
#include "../util/ByteArray.hpp"
#include "../util/InternedString.hpp"
#include "../util/string.hpp"

#include <absl/container/flat_hash_map.h>
#include <absl/hash/hash.h>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <tuple>

namespace nw {

struct Container;

// == ResourceType ============================================================
// ============================================================================

/// Resource type constants and helper functions
struct ResourceType {
    /// Enumeration of resource types.
    enum type : uint16_t {
        invalid = 0xFFFF,

        // Custom - Resource Type Categories
        container = 0xFFFF - 1,
        gff_archive = 0xFFFF - 2,
        movie = 0xFFFF - 3,
        player = 0xFFFF - 4,
        sound = 0xFFFF - 5,
        texture = 0xFFFF - 6,
        json = 0xFFFF - 7,
        object = 0xFFFF - 8,

        bmp = 1,
        mve = 2,
        tga = 3,
        wav = 4,
        plt = 6,
        ini = 7,
        bmu = 8,
        mpg = 9,
        txt = 10,
        plh = 2000,
        tex = 2001,
        mdl = 2002,
        thg = 2003,
        fnt = 2005,
        lua = 2007,
        slt = 2008,
        nss = 2009,
        ncs = 2010,
        mod = 2011,
        are = 2012,
        set = 2013,
        ifo = 2014,
        bic = 2015,
        wok = 2016,
        twoda = 2017,
        tlk = 2018,
        txi = 2022,
        git = 2023,
        bti = 2024,
        uti = 2025,
        btc = 2026,
        utc = 2027,
        dlg = 2029,
        itp = 2030,
        btt = 2031,
        utt = 2032,
        dds = 2033,
        bts = 2034,
        uts = 2035,
        ltr = 2036,
        gff = 2037,
        fac = 2038,
        bte = 2039,
        ute = 2040,
        btd = 2041,
        utd = 2042,
        btp = 2043,
        utp = 2044,
        dft = 2045,
        gic = 2046,
        gui = 2047,
        css = 2048,
        ccs = 2049,
        btm = 2050,
        utm = 2051,
        dwk = 2052,
        pwk = 2053,
        btg = 2054,
        utg = 2055,
        jrl = 2056,
        sav = 2057,
        utw = 2058,
        fourpc = 2059,
        ssf = 2060,
        hak = 2061,
        nwm = 2062,
        bik = 2063,
        ndb = 2064,
        ptm = 2065,
        ptt = 2066,
        bak = 2067,
        dat = 2068,
        shd = 2069,
        xbc = 2070,
        wbm = 2071,
        mtr = 2072,
        ktx = 2073,
        ttf = 2074,
        sql = 2075,
        tml = 2076,
        sq3 = 2077,
        lod = 2078,
        gif = 2079,
        png = 2080,
        jpg = 2081,
        caf = 2082,
        ids = 9996,
        erf = 9997,
        bif = 9998,
        key = 9999,
    };

    /// Converts extension to ResourceType::type
    static type from_extension(StringView ext)
    {
        if (ext.size() < 3) return invalid;
        if (ext[0] == '.') {
            ext = StringView(ext.data() + 1, ext.size() - 1);
        }

        if (string::icmp(ext, "bmp")) return bmp;
        if (string::icmp(ext, "mve")) return mve;
        if (string::icmp(ext, "tga")) return tga;
        if (string::icmp(ext, "wav")) return wav;
        if (string::icmp(ext, "plt")) return plt;
        if (string::icmp(ext, "ini")) return ini;
        if (string::icmp(ext, "bmu")) return bmu;
        if (string::icmp(ext, "mpg")) return mpg;
        if (string::icmp(ext, "txt")) return txt;
        if (string::icmp(ext, "plh")) return plh;
        if (string::icmp(ext, "tex")) return tex;
        if (string::icmp(ext, "mdl")) return mdl;
        if (string::icmp(ext, "thg")) return thg;
        if (string::icmp(ext, "fnt")) return fnt;
        if (string::icmp(ext, "lua")) return lua;
        if (string::icmp(ext, "slt")) return slt;
        if (string::icmp(ext, "nss")) return nss;
        if (string::icmp(ext, "ncs")) return ncs;
        if (string::icmp(ext, "mod")) return mod;
        if (string::icmp(ext, "are")) return are;
        if (string::icmp(ext, "set")) return set;
        if (string::icmp(ext, "ifo")) return ifo;
        if (string::icmp(ext, "bic")) return bic;
        if (string::icmp(ext, "wok")) return wok;
        if (string::icmp(ext, "2da")) return twoda;
        if (string::icmp(ext, "tlk")) return tlk;
        if (string::icmp(ext, "txi")) return txi;
        if (string::icmp(ext, "git")) return git;
        if (string::icmp(ext, "bti")) return bti;
        if (string::icmp(ext, "uti")) return uti;
        if (string::icmp(ext, "btc")) return btc;
        if (string::icmp(ext, "utc")) return utc;
        if (string::icmp(ext, "dlg")) return dlg;
        if (string::icmp(ext, "itp")) return itp;
        if (string::icmp(ext, "btt")) return btt;
        if (string::icmp(ext, "utt")) return utt;
        if (string::icmp(ext, "dds")) return dds;
        if (string::icmp(ext, "bts")) return bts;
        if (string::icmp(ext, "uts")) return uts;
        if (string::icmp(ext, "ltr")) return ltr;
        if (string::icmp(ext, "gff")) return gff;
        if (string::icmp(ext, "fac")) return fac;
        if (string::icmp(ext, "bte")) return bte;
        if (string::icmp(ext, "ute")) return ute;
        if (string::icmp(ext, "btd")) return btd;
        if (string::icmp(ext, "utd")) return utd;
        if (string::icmp(ext, "btp")) return btp;
        if (string::icmp(ext, "utp")) return utp;
        if (string::icmp(ext, "dft")) return dft;
        if (string::icmp(ext, "gic")) return gic;
        if (string::icmp(ext, "gui")) return gui;
        if (string::icmp(ext, "css")) return css;
        if (string::icmp(ext, "ccs")) return ccs;
        if (string::icmp(ext, "btm")) return btm;
        if (string::icmp(ext, "utm")) return utm;
        if (string::icmp(ext, "dwk")) return dwk;
        if (string::icmp(ext, "pwk")) return pwk;
        if (string::icmp(ext, "btg")) return btg;
        if (string::icmp(ext, "utg")) return utg;
        if (string::icmp(ext, "jrl")) return jrl;
        if (string::icmp(ext, "sav")) return sav;
        if (string::icmp(ext, "utw")) return utw;
        if (string::icmp(ext, "4pc")) return fourpc;
        if (string::icmp(ext, "ssf")) return ssf;
        if (string::icmp(ext, "hak")) return hak;
        if (string::icmp(ext, "nwm")) return nwm;
        if (string::icmp(ext, "bik")) return bik;
        if (string::icmp(ext, "ndb")) return ndb;
        if (string::icmp(ext, "ptm")) return ptm;
        if (string::icmp(ext, "ptt")) return ptt;
        if (string::icmp(ext, "bak")) return bak;
        if (string::icmp(ext, "dat")) return dat;
        if (string::icmp(ext, "shd")) return shd;
        if (string::icmp(ext, "xbc")) return xbc;
        if (string::icmp(ext, "wbm")) return wbm;
        if (string::icmp(ext, "mtr")) return mtr;
        if (string::icmp(ext, "ktx")) return ktx;
        if (string::icmp(ext, "ttf")) return ttf;
        if (string::icmp(ext, "sql")) return sql;
        if (string::icmp(ext, "tml")) return tml;
        if (string::icmp(ext, "sq3")
            || string::icmp(ext, "sqlite3")
            || string::icmp(ext, "sqlite3nwnxee")) return sq3;
        if (string::icmp(ext, "lod")) return lod;
        if (string::icmp(ext, "gif")) return gif;
        if (string::icmp(ext, "png")) return png;
        if (string::icmp(ext, "jpg")) return jpg;
        if (string::icmp(ext, "caf")) return caf;
        if (string::icmp(ext, "ids")) return ids;
        if (string::icmp(ext, "erf")) return erf;
        if (string::icmp(ext, "bif")) return bif;
        if (string::icmp(ext, "key")) return key;

        // Custom types
        if (string::icmp(ext, "json")) return json;

        return invalid;
    }

    /**
     * @brief Convert ResourceType::type to extension
     * @note The only compilers and standard libraries that are targeted have small string optimization,
     *       so there is no great overhead to just returning a ``String``
     * @return extension or empty string on failure
     */
    static StringView to_string(ResourceType::type value)
    {
        switch (value) {
        default:
            return {};
        case bmp:
            return "bmp";
        case mve:
            return "mve";
        case tga:
            return "tga";
        case wav:
            return "wav";
        case plt:
            return "plt";
        case ini:
            return "ini";
        case bmu:
            return "bmu";
        case mpg:
            return "mpg";
        case txt:
            return "txt";
        case plh:
            return "plh";
        case tex:
            return "tex";
        case mdl:
            return "mdl";
        case thg:
            return "thg";
        case fnt:
            return "fnt";
        case lua:
            return "lua";
        case slt:
            return "slt";
        case nss:
            return "nss";
        case ncs:
            return "ncs";
        case mod:
            return "mod";
        case are:
            return "are";
        case set:
            return "set";
        case ifo:
            return "ifo";
        case bic:
            return "bic";
        case wok:
            return "wok";
        case twoda:
            return "2da";
        case tlk:
            return "tlk";
        case txi:
            return "txi";
        case git:
            return "git";
        case bti:
            return "bti";
        case uti:
            return "uti";
        case btc:
            return "btc";
        case utc:
            return "utc";
        case dlg:
            return "dlg";
        case itp:
            return "itp";
        case btt:
            return "btt";
        case utt:
            return "utt";
        case dds:
            return "dds";
        case bts:
            return "bts";
        case uts:
            return "uts";
        case ltr:
            return "ltr";
        case gff:
            return "gff";
        case fac:
            return "fac";
        case bte:
            return "bte";
        case ute:
            return "ute";
        case btd:
            return "btd";
        case utd:
            return "utd";
        case btp:
            return "btp";
        case utp:
            return "utp";
        case dft:
            return "dft";
        case gic:
            return "gic";
        case gui:
            return "gui";
        case css:
            return "css";
        case ccs:
            return "ccs";
        case btm:
            return "btm";
        case utm:
            return "utm";
        case dwk:
            return "dwk";
        case pwk:
            return "pwk";
        case btg:
            return "btg";
        case utg:
            return "utg";
        case jrl:
            return "jrl";
        case sav:
            return "sav";
        case utw:
            return "utw";
        case fourpc:
            return "4pc";
        case ssf:
            return "ssf";
        case hak:
            return "hak";
        case nwm:
            return "nwm";
        case bik:
            return "bik";
        case ndb:
            return "ndb";
        case ptm:
            return "ptm";
        case ptt:
            return "ptt";
        case bak:
            return "bak";
        case dat:
            return "dat";
        case shd:
            return "shd";
        case xbc:
            return "xbc";
        case wbm:
            return "wbm";
        case mtr:
            return "mtr";
        case ktx:
            return "ktx";
        case ttf:
            return "ttf";
        case sql:
            return "sql";
        case tml:
            return "tml";
        case sq3:
            return "sq3";
        case lod:
            return "lod";
        case gif:
            return "gif";
        case png:
            return "png";
        case jpg:
            return "jpg";
        case caf:
            return "caf";
        case ids:
            return "ids";
        case erf:
            return "erf";
        case bif:
            return "bif";
        case key:
            return "key";
        case json:
            return "json";
        }
    }

    static constexpr bool check_category(ResourceType::type category, ResourceType::type type)
    {
        if (category == type) { return true; }
        switch (category) {
        default:
            return false;
        case ResourceType::container:
            return type == ResourceType::bif
                || type == ResourceType::erf
                || type == ResourceType::key
                || type == ResourceType::mod
                || type == ResourceType::nwm
                || type == ResourceType::sav;
        case ResourceType::object: // We leave out areas, modules, and players
            return type == ResourceType::utc
                || type == ResourceType::utd
                || type == ResourceType::ute
                || type == ResourceType::uti
                || type == ResourceType::utm
                || type == ResourceType::utp
                || type == ResourceType::uts
                || type == ResourceType::utt
                || type == ResourceType::utw;
        case ResourceType::gff_archive:
            return type == ResourceType::are
                || type == ResourceType::bic
                || type == ResourceType::caf
                || type == ResourceType::dlg
                || type == ResourceType::fac
                || type == ResourceType::gff
                || type == ResourceType::gic
                || type == ResourceType::git
                || type == ResourceType::ifo
                || type == ResourceType::itp
                || type == ResourceType::jrl
                || type == ResourceType::utc
                || type == ResourceType::utd
                || type == ResourceType::ute
                || type == ResourceType::uti
                || type == ResourceType::utm
                || type == ResourceType::utp
                || type == ResourceType::uts
                || type == ResourceType::utt
                || type == ResourceType::utw;
        case ResourceType::movie:
            return type == ResourceType::wbm;
        case ResourceType::player:
            return type == ResourceType::bic;
        case ResourceType::sound:
            return type == ResourceType::bmu
                || type == ResourceType::wav;
        case ResourceType::texture:
            return type == ResourceType::dds
                || type == ResourceType::tga
                || type == ResourceType::plt;
        }
    }
};

// == Resref ==================================================================
// ============================================================================

/**
 * @brief ``nw::Resref`` names a resource.
 */
struct Resref {
    using size_type = size_t;
    static constexpr size_type maximum_size = 4096;

    Resref();
    Resref(const Resref&) = default;

    template <size_t N>
    Resref(std::array<char, N>& string) noexcept;

    Resref(const char* string) noexcept;
    Resref(StringView string) noexcept;

    Resref& operator=(const Resref&) = default;

    /// Checks if the underlying array has no non-null characters
    bool empty() const noexcept;

    /// Returns the number of char elements in the array, excluding nulls.
    size_type length() const noexcept;

    /// Creates ``String`` of underlying array
    String string() const;

    /// Creates ``StringView`` of underlying array without null padding
    StringView view() const noexcept;

private:
    template <typename H>
    friend H AbslHashValue(H h, const Resref& res);

    InternedString data_;
};

template <size_t N>
Resref::Resref(std::array<char, N>& string) noexcept
{
    static_assert(N < maximum_size, "[resref] invalid size");
    std::transform(string.begin(), string.end(), string.begin(), [](char c) {
        return static_cast<char>(::tolower(c));
    });

    size_t null = 0;
    while (string[null] && null < N) {
        ++null;
    }
    data_ = nw::kernel::strings().intern(StringView{string.data(), null});
}

inline bool operator==(const Resref& lhs, const Resref& rhs)
{
    return lhs.view() == rhs.view();
}

inline bool operator<(const Resref& lhs, const Resref& rhs)
{
    return lhs.view() < rhs.view();
}

std::ostream& operator<<(std::ostream& out, const Resref& resref);

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, Resref& r);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, const Resref& r);

template <typename H>
H AbslHashValue(H h, const Resref& res)
{
    return H::combine(std::move(h), res.data_.ptr());
}

// == Resource ================================================================
// ============================================================================

/**
 * @brief A ``nw::Resource`` consists of a ``nw::Resref`` and a ``nw::ResourceType``.
 * Since NWN1/EE doesn't have any notion of hierarchical organization (paths, etc),
 * it represents a fully-qualified resource identifier.
 */
struct Resource {
    Resource() noexcept;
    Resource(const Resref& resref_, ResourceType::type type_) noexcept;
    Resource(StringView resref_, ResourceType::type type_) noexcept;
    Resource(const Resource&) = default;
    Resource(Resource&&) = default;

    static Resource from_filename(StringView);
    static Resource from_path(const std::filesystem::path& path);

    Resref resref;
    ResourceType::type type;

    /// Gets a Resrefs file name with extension.
    String filename() const;

    /// A resource is valid if resref is not empty and resref type is not invalid
    bool valid() const noexcept;

    Resource& operator=(const Resource&) = default;
    Resource& operator=(Resource&&) = default;
};

template <typename H>
H AbslHashValue(H h, const Resource& res)
{
    return H::combine(std::move(h), res.resref, res.type);
}

inline bool operator==(const Resource& lhs, const Resource& rhs)
{
    return std::tie(lhs.resref, lhs.type) == std::tie(rhs.resref, rhs.type);
}

inline bool operator<(const Resource& lhs, const Resource& rhs)
{
    return std::tie(lhs.resref, lhs.type) < std::tie(rhs.resref, rhs.type);
}

std::ostream& operator<<(std::ostream& out, const Resource& res);

/// nlohmann::json specialization
void from_json(const nlohmann::json& j, Resource& r);

/// nlohmann::json specialization
void to_json(nlohmann::json& j, const Resource& r);

// == ResourceRegistry ========================================================
// ============================================================================

struct ContainerKey;
struct ResourceData;

struct ResourceRegistry {
private:
    struct Entry {
        Container* container;
        const ContainerKey* key;
    };

    absl::flat_hash_map<Resource, Entry> entries_;

    const Entry* lookup(Resource uri) const;

public:
    void clear();
    bool contains(Resource uri) const noexcept;
    ResourceData demand(Resource uri) const;
    void insert(Resource uri, Container* container, const ContainerKey* key);
    void reserve(size_t size);

    /// Gets the number of resources in the registry
    size_t size() const noexcept;

    /// Executes the callback for every resource in the registry
    void visit(std::function<void(Resource)> visitor) const;
};

// == ResourceData ============================================================
// ============================================================================

struct ResourceData {
    ResourceData() = default;
    ResourceData(const ResourceData&) = delete;
    ResourceData(ResourceData&&) = default;
    ResourceData& operator=(const ResourceData&) = delete;
    ResourceData& operator=(ResourceData&&) = default;

    Resource name;
    ByteArray bytes;

    bool operator==(const ResourceData& other) const = default;
    ResourceData copy() const;

    static ResourceData from_file(const std::filesystem::path& path);
};

} // namespace nw

template <>
struct fmt::formatter<nw::Resref> : fmt::formatter<nw::StringView> {
    // parse is inherited from formatter<string_view>.
    template <typename FormatContext>
    auto format(const nw::Resref& r, FormatContext& ctx) const
    {
        return formatter<string_view>::format(r.view(), ctx);
    }
};
