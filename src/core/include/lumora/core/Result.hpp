#pragma once

#include <lumora/core/Error.hpp>

#include <utility>
#include <variant>

namespace lumora::core {

template<typename T, typename E = Error>
class [[nodiscard]] Result final {
public:
    [[nodiscard]] static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    [[nodiscard]] static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return valueOrError_.index() == 0;
    }

    [[nodiscard]] T& value() & {
        return std::get<0>(valueOrError_);
    }

    [[nodiscard]] const T& value() const& {
        return std::get<0>(valueOrError_);
    }

    [[nodiscard]] T&& value() && {
        return std::get<0>(std::move(valueOrError_));
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(valueOrError_);
    }

    [[nodiscard]] const E& error() const& {
        return std::get<1>(valueOrError_);
    }

    [[nodiscard]] E&& error() && {
        return std::get<1>(std::move(valueOrError_));
    }

private:
    template<typename... Args>
    explicit Result(std::in_place_index_t<0> index, Args&&... args)
        : valueOrError_(index, std::forward<Args>(args)...) {}

    template<typename... Args>
    explicit Result(std::in_place_index_t<1> index, Args&&... args)
        : valueOrError_(index, std::forward<Args>(args)...) {}

    std::variant<T, E> valueOrError_;
};

template<typename E>
class [[nodiscard]] Result<void, E> final {
public:
    [[nodiscard]] static Result success() {
        return Result(std::in_place_index<0>);
    }

    [[nodiscard]] static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return valueOrError_.index() == 0;
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(valueOrError_);
    }

    [[nodiscard]] const E& error() const& {
        return std::get<1>(valueOrError_);
    }

    [[nodiscard]] E&& error() && {
        return std::get<1>(std::move(valueOrError_));
    }

private:
    explicit Result(std::in_place_index_t<0> index)
        : valueOrError_(index) {}

    explicit Result(std::in_place_index_t<1> index, E error)
        : valueOrError_(index, std::move(error)) {}

    std::variant<std::monostate, E> valueOrError_;
};

}  // namespace lumora::core
