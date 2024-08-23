#pragma once

#include <type_traits>
#include <utility>

namespace rlib::intrusive {
    template<typename X, typename T, template<class> class K>
    concept NodeGetter = requires(X x, T& element, T* elementPtr) {
        typename std::remove_reference_t<X>::Element;
        requires std::is_same_v<typename std::remove_reference_t<X>::Element, T>;

        { x(element) } -> std::same_as<K<T>&>;

        { x(elementPtr) } -> std::same_as<K<T>&>;
    };

    template<class T, template<class> class K>
    struct NodeFromBase {
        using Element = T;

        K<T>& operator()(T& element) const { return element; }

        K<T>& operator()(T* element) const { return *element; }
    };

    template<class T, template<class> class K, K<T> T::*Node>
    struct NodeFromMember {
        using Element = T;
        
        K<T>& operator()(T& element) const { return element.*Node; }

        K<T>& operator()(T* element) const { return element->*Node; }
    };

    template<class NG, class Func>
    auto map(NG a, Func b)
    {
        using ResultA = std::invoke_result_t<NG, typename NG::Element&>;
        using MapResult = std::invoke_result_t<Func, ResultA&>;
        
        struct Mapped {
            using Element = NG::Element;

            Mapped(NG a, Func b) : a(std::move(a)), b(std::move(b)) {}

            MapResult& operator()(Element& element) const { return b(a(element)); }

            MapResult& operator()(Element* element) const { return this->operator()(*element); }

        private:
            [[no_unique_address]] NG a;
            [[no_unique_address]] Func    b;
        };

        return Mapped(std::move(a), std::move(b));
    }

} // namespace rlib::intrusive
