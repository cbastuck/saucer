#pragma once

#include "glaze.hpp"

#include <fmt/xchar.h>
#include <fmt/format.h>

#include <boost/callable_traits.hpp>

#include <tl/expected.hpp>
#include <rebind/name.hpp>

namespace saucer::serializers::glaze
{
    namespace impl
    {
        static constexpr auto opts = glz::opts{.error_on_missing_keys = true};

        template <typename... Ts>
        consteval auto decay_tuple_impl(const std::tuple<Ts...> &) -> std::tuple<std::decay_t<Ts>...>;

        template <typename T>
        using decay_tuple_t = decltype(decay_tuple_impl(std::declval<T>()));

        template <typename... Ts>
        consteval auto drop_last_impl(const std::tuple<Ts...> &tuple)
        {
            auto unpack = [&]<auto... Is>(std::index_sequence<Is...>)
            {
                return std::make_tuple(std::get<Is>(tuple)...);
            };

            return unpack(std::make_index_sequence<sizeof...(Ts) - 1>());
        }

        template <typename T>
        using drop_last_t = decltype(drop_last_impl(std::declval<T>()));

        template <typename... Ts>
        consteval auto last_impl(const std::tuple<Ts...> &tuple)
        {
            if constexpr (sizeof...(Ts) > 0)
            {
                return std::get<sizeof...(Ts) - 1>(tuple);
            }
        }

        template <typename T>
        using last_t = decltype(last_impl(std::declval<T>()));

        template <launch Policy, typename T>
        struct args_impl
        {
            using type = decay_tuple_t<boost::callable_traits::args_t<T>>;
        };

        template <typename T>
        struct args_impl<launch::manual, T>
        {
            using args = decay_tuple_t<boost::callable_traits::args_t<T>>;
            using type = drop_last_t<args>;
        };

        template <launch Policy, typename T>
        using args_t = args_impl<Policy, T>::type;

        template <launch Policy, typename T>
        struct return_impl
        {
            using type = boost::callable_traits::return_type_t<T>;
        };

        template <typename T>
        struct return_impl<launch::manual, T>
        {
            using executor = last_t<boost::callable_traits::args_t<T>>;
            using type     = executor::type;
        };

        template <launch Policy, typename T>
        using return_t = return_impl<Policy, T>::type;

        template <typename T>
        struct serializable_impl : std::false_type
        {
        };

        template <>
        struct serializable_impl<void> : std::true_type
        {
        };

        template <>
        struct serializable_impl<std::tuple<>> : std::true_type
        {
        };

        template <typename T>
            requires glz::read_supported<opts.format, T>
        struct serializable_impl<T> : std::true_type
        {
        };

        template <typename T>
            requires saucer::is_arguments<T>
        struct serializable_impl<T> : std::true_type
        {
        };

        template <typename T>
        inline constexpr bool serializable = serializable_impl<T>::value;

        template <typename T, std::size_t I = 0>
        error mismatch(T &tuple, const glz::json_t &json)
        {
            static constexpr auto N = std::tuple_size_v<T>;

            if constexpr (I < N)
            {
                using current_t = std::tuple_element_t<I, T>;
                auto serialized = glz::write<opts>(json[I]).value_or("");

                if (auto err = glz::read<opts>(std::get<I>(tuple), serialized); !err)
                {
                    return mismatch<T, I + 1>(tuple, json);
                }

                return {
                    error_code::type_mismatch,
                    fmt::format("Expected parameter {} to be '{}'", I, rebind::type_name<current_t>),
                };
            }
            else
            {
                return {error_code::unknown};
            }
        }

        template <typename T>
            requires(std::tuple_size_v<T> != 0)
        tl::expected<T, error> parse(const std::string &data)
        {
            T rtn{};

            if (auto err = glz::read<opts>(rtn, data); !err)
            {
                return rtn;
            }

            glz::json_t json{};

            if (auto err = glz::read<opts>(json, data); err)
            {
                auto name = std::string{glz::nameof(err.ec)};
                return tl::make_unexpected(error{error_code::unknown, name});
            }

            return tl::make_unexpected(mismatch<T>(rtn, json));
        }

        template <typename T>
            requires(std::tuple_size_v<T> == 0)
        tl::expected<T, error> parse(const std::string &)
        {
            return {};
        }

        template <typename T>
        auto serialize_arg(const T &value)
        {
            static_assert(serializable<T>, "Given type is not serializable");

            auto json    = glz::write<opts>(value).value_or("null");
            auto escaped = glz::write<opts>(json).value_or("null");

            return fmt::format("JSON.parse({})", escaped);
        }

        template <typename T>
            requires is_arguments<T>
        auto serialize_arg(const T &value)
        {
            std::vector<std::string> rtn;
            rtn.reserve(value.size());

            auto unpack = [&]<typename... Ts>(Ts &&...args)
            {
                (rtn.emplace_back(serialize_arg(std::forward<Ts>(args))), ...);
            };
            std::apply(unpack, value.as_tuple());

            return fmt::format("{}", fmt::join(rtn, ", "));
        }

        template <typename T>
        auto serialize_res(T &&callback)
        {
            std::string result{"undefined"};

            if constexpr (!std::is_void_v<std::invoke_result_t<T>>)
            {
                result = impl::serialize_arg(callback());
            }
            else
            {
                std::invoke(std::forward<T>(callback));
            }

            return result;
        }
    } // namespace impl

    template <launch Policy, typename Function>
    auto serializer::serialize(const Function &func)
    {
        using result_t = impl::return_t<Policy, Function>;
        using args_t   = impl::args_t<Policy, Function>;

        static_assert(impl::serializable<result_t> && impl::serializable<args_t>,
                      "All arguments as well as the result type must be serializable");

        static_assert(!is_executor<impl::last_t<args_t>>, "Usage of executor requires launch::manual");

        return [func](saucer::function_data &data, const executor &exec)
        {
            const auto &[resolve, reject] = exec;

            const auto &message = static_cast<function_data &>(data);
            const auto params   = impl::parse<args_t>(message.params.str);

            if (!params)
            {
                return std::invoke(reject, params.error());
            }

            if constexpr (Policy == launch::manual)
            {
                auto exec_resolve = [resolve]<typename... Ts>(Ts &&...value)
                {
                    std::invoke(resolve, impl::serialize_res([&value...]() { return (value, ...); }));
                };

                auto exec_reject = [reject](std::string reason)
                {
                    std::invoke(reject, error{error_code::rejected, std::move(reason)});
                };

                auto exec_param = saucer::executor<result_t>{exec_resolve, exec_reject};
                auto all_params = std::tuple_cat(params.value(), std::make_tuple(std::move(exec_param)));

                std::apply(func, all_params);
            }
            else
            {
                std::invoke(resolve, impl::serialize_res([&]() { return std::apply(func, params.value()); }));
            }
        };
    }

    template <typename... Ts>
    auto serializer::serialize_args(const Ts &...params)
    {
        serializer::args rtn;

        rtn.reserve(sizeof...(params), 0);
        (rtn.push_back(impl::serialize_arg(params)), ...);

        return rtn;
    }

    template <typename T>
    auto serializer::resolve(std::shared_ptr<std::promise<T>> promise)
    {
        static_assert(impl::serializable<T>, "The promise result must be serializable");

        return [promise](saucer::result_data &data) mutable
        {
            const auto &result = static_cast<result_data &>(data);

            if constexpr (!std::is_void_v<T>)
            {
                T value{};

                if (auto error = glz::read<impl::opts>(value, result.result.str); error)
                {
                    auto exception = std::runtime_error{std::string{glz::nameof(error.ec)}};
                    promise->set_exception(std::make_exception_ptr(exception));
                    return;
                }

                promise->set_value(value);
            }
            else
            {
                promise->set_value();
            }
        };
    }
} // namespace saucer::serializers::glaze
