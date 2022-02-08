#include "ResourceLocator.hpp"

#include "../log.hpp"
#include "../util/string.hpp"
#include "../util/templates.hpp"
#include "Directory.hpp"
#include "Erf.hpp"
#include "Key.hpp"
#include "NWSync.hpp"
#include "Zip.hpp"

namespace nw {

bool ResourceLocator::add_container(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        LOG_F(ERROR, "path does not exist: {}", path.c_str());
        return false;
    }

    if (std::filesystem::is_directory(path)) {
        return add_container(new Directory(path));
    } else if (string::icmp(path.extension().c_str(), ".erf")
        || string::icmp(path.extension().c_str(), ".mod")
        || string::icmp(path.extension().c_str(), ".hak")) {
        return add_container(new Erf(path));
    } else if (string::icmp(path.extension().c_str(), ".key")) {
        return add_container(new Key(path));
    } else if (string::icmp(path.extension().c_str(), ".zip")) {
        return add_container(new Zip(path));
    } else if (string::icmp(path.extension().c_str(), ".sav")) {
        LOG_F(ERROR, ".sav files are not currently supported: {}", path.c_str());
        return false;
    }

    return false;
}

bool ResourceLocator::add_container(Container* container, bool take_ownership)
{
    if (take_ownership) {
        containers_.emplace_back(std::unique_ptr<Container>(container));
    } else {
        containers_.push_back(container);
    }

    return true;
}

bool ResourceLocator::contains(const Resource& res) const
{
    return !!stat(res);
}

ByteArray ResourceLocator::demand(Resource res) const
{
    ByteArray result;
    for (const auto& c : reverse(containers_)) {
        if (std::holds_alternative<Container*>(c)) {
            result = std::get<Container*>(c)->demand(res);
        } else {
            result = std::get<std::unique_ptr<Container>>(c)->demand(res);
        }
        if (result.size()) { break; }
    }
    return result;
}

int ResourceLocator::extract_by_glob(std::string_view glob, const std::filesystem::path& output) const
{
    int result = 0;
    for (const auto& c : containers_) {
        if (std::holds_alternative<Container*>(c)) {
            result += std::get<Container*>(c)->extract_by_glob(glob, output);
        } else {
            result += std::get<std::unique_ptr<Container>>(c)->extract_by_glob(glob, output);
        }
    }
    return result;
}

int ResourceLocator::extract(const std::regex& pattern, const std::filesystem::path& output) const
{
    int result = 0;
    for (const auto& c : containers_) {
        if (std::holds_alternative<Container*>(c)) {
            result += std::get<Container*>(c)->extract(pattern, output);
        } else {
            result += std::get<std::unique_ptr<Container>>(c)->extract(pattern, output);
        }
    }
    return result;
}

size_t ResourceLocator::size() const
{
    size_t result = 0;
    for (const auto& c : reverse(containers_)) {
        if (std::holds_alternative<Container*>(c)) {
            result += std::get<Container*>(c)->size();
        } else {
            result += std::get<std::unique_ptr<Container>>(c)->size();
        }
    }
    return result;
}

ResourceDescriptor ResourceLocator::stat(const Resource& res) const
{
    ResourceDescriptor result;
    for (const auto& c : reverse(containers_)) {
        if (std::holds_alternative<Container*>(c)) {
            result = std::get<Container*>(c)->stat(res);
        } else {
            result = std::get<std::unique_ptr<Container>>(c)->stat(res);
        }
        if (result) { break; }
    }
    return result;
}

} // namespace nw
