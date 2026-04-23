#include "Txi.hpp"

#include "../util/Tokenizer.hpp"

#include <string>

namespace nw {

Txi::Txi(ResourceData data)
    : data_{std::move(data)}
{
    loaded_ = parse();
}

bool Txi::get_to(String key, String& out) const
{
    string::tolower(&key);
    auto it = map_.find(key);
    if (it == std::end(map_)) {
        return false;
    }
    out = it->second;
    return true;
}

bool Txi::valid() const noexcept
{
    return loaded_;
}

bool Txi::parse()
{
    if (data_.bytes.size() == 0) {
        return false;
    }

    const auto* chars = reinterpret_cast<const char*>(data_.bytes.data());
    StringView view{chars, data_.bytes.size()};
    Tokenizer tokens{view, "#", false};
    while (true) {
        auto key = tokens.next();
        if (key.empty()) {
            break;
        }
        if (tokens.is_newline(key)) {
            continue;
        }

        String lowered{key};
        string::tolower(&lowered);

        std::string value;
        while (true) {
            auto token = tokens.next();
            if (token.empty() || tokens.is_newline(token)) {
                break;
            }
            if (!value.empty()) {
                value.push_back(' ');
            }
            value.append(token.begin(), token.end());
        }

        map_[std::move(lowered)] = std::move(value);
    }

    return true;
}

} // namespace nw
