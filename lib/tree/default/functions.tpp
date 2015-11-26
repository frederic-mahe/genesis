/**
 * @brief Implementation of Default Tree functions.
 *
 * @file
 * @ingroup tree
 */

#include "utils/string/string.hpp"

namespace genesis {

// =================================================================================================
//     Find
// =================================================================================================

template <class TreeType>
typename TreeType::NodeType* find_node(TreeType& tree, const std::string& name)
{
    auto clean_name = string_replace_all(name, "_", " ");

    for (auto it = tree.begin_nodes(); it != tree.end_nodes(); ++it) {
        if ((*it)->data.name == clean_name) {
            return it->get();
        }
    }

    return nullptr;
}

} // namespace genesis
