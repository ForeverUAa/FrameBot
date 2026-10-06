#pragma once

#include <Geode/ui/Popup.hpp>

#include <tuple>
#include <type_traits>
#include <utility>

namespace framebot {

template <class... Args>
class Popup : public geode::Popup {
protected:
    virtual bool setup(Args...) = 0;

private:
    template <class Tuple, std::size_t... I>
    bool callSetup(Tuple&& args, std::index_sequence<I...>) {
        return setup(std::get<I>(std::forward<Tuple>(args))...);
    }

public:
    void closePopup() {
        this->onClose(nullptr);
    }

    template <class... InitArgs>
    bool initAnchored(float width, float height, InitArgs&&... args) {
        constexpr std::size_t count = sizeof...(InitArgs);
        static_assert(count >= 1);

        auto tuple = std::forward_as_tuple(std::forward<InitArgs>(args)...);

        if constexpr (count == 1) {
            if (!geode::Popup::init(width, height, std::get<0>(tuple)))
                return false;

            static_assert(sizeof...(Args) == 0);
            return setup();
        }
        else if constexpr (
            count == 2 &&
            std::is_convertible_v<std::tuple_element_t<0, decltype(tuple)>, char const*> &&
            std::is_same_v<std::remove_cvref_t<std::tuple_element_t<1, decltype(tuple)>>, cocos2d::CCRect>
        ) {
            if (!geode::Popup::init(width, height, std::get<0>(tuple), std::get<1>(tuple)))
                return false;

            return setup();
        }
        else {
            constexpr std::size_t argCount = count - 1;
            if (!geode::Popup::init(width, height, std::get<argCount>(tuple)))
                return false;

            return callSetup(tuple, std::make_index_sequence<argCount>{});
        }
    }
};

}
