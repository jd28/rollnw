#pragma once

namespace nw::kernel {

struct Services;
struct Config;
struct ObjectSystem;
struct Resources;
struct Rules;
struct Strings;

Services& services();
Config& config();
ObjectSystem& objects();
Resources& resman();
Rules& rules();
Strings& strings();

} // namespace nw::kernel
