#pragma once

#include <type_traits>

namespace rlib::intrusive::detail {

    template<class T>
    struct SkipListNode {
        SkipListNode() = default;

        SkipListNode(const SkipListNode&) = delete;

        SkipListNode& operator=(const SkipListNode&) = delete;

        rlib::OwningPointer<ListNode<T>[]> links;
    };

    template<class T, NodeGetter<T, SkipListNode> NG>
    void unlink(SkipListNode<T>& head, T& element, NG&& nodeGetter)
    {
        auto& node = nodeGetter(element);

        for (auto index = std::size_t(0); index < node.links.size(); index++) {
            auto selector =
                map(std::forward<NG>(nodeGetter), [=](SkipListNode<T>& node) -> auto& { return node.links[index]; });
            unlink(head.links[index], node.links[index], selector);
        }
    }

    template<class T, class Project>
    using Projected = std::remove_cvref_t<std::invoke_result_t<Project, const T&>>;

    template<class T, class LessThan, class Project, bool = std::is_same_v<Projected<T, Project>, T>>
    struct Comparator {
        bool operator()(const Projected<T, Project>& a, const T& b) const
        {
            auto lessThan = LessThan{};
            auto project  = Project{};
            return lessThan(a, project(b));
        }

        bool operator()(const T& a, const Projected<T, Project>& b) const
        {
            auto lessThan = LessThan{};
            auto project  = Project{};
            return lessThan(project(a), b);
        }

        bool operator()(const T& a, const T& b) const
        {
            auto lessThan = LessThan{};
            auto project  = Project{};
            return lessThan(project(a), project(b));
        }
    };

    // Specialization when Projected<T, Project> and T are the same type
    template<class T, class LessThan, class Project>
    struct Comparator<T, LessThan, Project, true> {
        bool operator()(const T& a, const T& b) const { return LessThan{}(a, b); }
    };

    struct NodeFromRootLayer {
        template<class T>
        ListNode<T>& operator()(SkipListNode<T>& element) const
        {
            return element.links[0];
        }
    };

    template<class T, NodeGetter<T, SkipListNode> NG>
    class ListCursor {
    public:
        ListCursor(SkipListNode<T>* head, std::size_t layer) : head(head), position(head), layer(layer) {}

        void advance() { position = &NG{}(position->links[layer - 1].next); }

        T* next() { return position->links[layer - 1].next; }

        void addTo(T& target)
        {
            auto mapped = map(NG{}, [this](SkipListNode<T>& node) -> auto& { return node.links[layer - 1]; });
            link(head->links[layer - 1], target, position->links[layer - 1], mapped);
        }

        bool descend() { return (layer > 1) ? (--layer, true) : false; }

        auto toIter() { return ListIterator(position->links[0], map(NG{}, detail::NodeFromRootLayer{})); }

    private:
        SkipListNode<T>* head;
        SkipListNode<T>* position;
        std::size_t      layer;
    };

} // namespace rlib::intrusive::detail
