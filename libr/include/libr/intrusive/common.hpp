#pragma once

namespace rlib::intrusive {
    template<class T, template<class> class K>
    struct NodeFromBase {
        static K<T>& get(T* element) { return *element; }
    };

    template<class T, template<class> class K, K<T> T::*Node>
    struct NodeFromMember {
        static K<T>& get(T* element) { return element->*Node; }
    };

   

} // namespace rlib::intrusive