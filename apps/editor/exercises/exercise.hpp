#pragma once
#include <string_view>

namespace darkangel::editor_app::exercises {
class Exercise {
public:
    virtual ~Exercise() = default;
    virtual void frame(unsigned frame) = 0;
    virtual void finish() = 0;
    virtual bool done() const = 0;
    virtual std::string_view result() const = 0;
};
}
