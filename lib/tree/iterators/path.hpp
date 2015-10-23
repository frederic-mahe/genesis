#ifndef GENESIS_TREE_ITERATORS_PATH_H_
#define GENESIS_TREE_ITERATORS_PATH_H_

/**
 * @brief
 *
 * @file
 * @ingroup tree
 */

#include <assert.h>
#include <deque>
#include <iterator>

#include "tree/tree_link.hpp"
#include "tree/tree_node.hpp"
#include "tree/tree_edge.hpp"

namespace genesis {

// =================================================================================================
//     Path Iterator
// =================================================================================================

/*

template <typename LinkPointerType, typename NodePointerType, typename EdgePointerType>
class TreeIteratorPath
{
public:
    // -----------------------------------------------------
    //     Typedefs
    // -----------------------------------------------------

    typedef TreeIteratorPath<LinkPointerType, NodePointerType, EdgePointerType> self_type;
    typedef std::forward_iterator_tag iterator_category;

    // -----------------------------------------------------
    //     Constructor
    // -----------------------------------------------------

    TreeIteratorPath (LinkPointerType from, LinkPointerType to) : start_(from), to_(to);
    {
        if (link) {
            stack_.push_back(link);
            if (link->outer() != link) {
                stack_.push_front(link->outer());
                link = link->outer();
            }
            while (link->is_inner()) {
                push_front_children(link);
                link = link->next()->outer();
            }
            assert(link == stack_.front());
            stack_.pop_front();
        }
        link_ = link;
    }

    // -----------------------------------------------------
    //     Operators
    // -----------------------------------------------------

    inline self_type operator ++ ()
    {
        if (stack_.empty()) {
            link_ = nullptr;
        } else if (link_->outer()->next() == stack_.front()) {
            link_ = stack_.front();
            stack_.pop_front();
        } else {
            link_ = stack_.front();
            while (link_->is_inner()) {
                push_front_children(link_);
                link_ = link_->next()->outer();
            }
            assert(link_ == stack_.front());
            stack_.pop_front();
        }

        return *this;
    }

    inline self_type operator ++ (int)
    {
        self_type tmp = *this;
        ++(*this);
        return tmp;
    }

    inline bool operator == (const self_type &other) const
    {
        return other.link_ == link_;
    }

    inline bool operator != (const self_type &other) const
    {
        return !(other == *this);
    }

    // -----------------------------------------------------
    //     Members
    // -----------------------------------------------------

    inline LinkPointerType link() const
    {
        return link_;
    }

    inline NodePointerType node() const
    {
        return link_->node();
    }

    inline EdgePointerType edge() const
    {
        return link_->edge();
    }

    inline LinkPointerType from_link() const
    {
        return from_;
    }

    inline NodePointerType from_node() const
    {
        return from_->node();
    }

    inline LinkPointerType to_link() const
    {
        return to_;
    }

    inline NodePointerType to_node() const
    {
        return to_->node();
    }

protected:
    LinkPointerType             link_;
    LinkPointerType             from_;
    LinkPointerType             to_;
    std::deque<LinkPointerType> stack_;
};

*/

} // namespace genesis

#endif // include guard