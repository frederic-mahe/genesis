/**
 * @brief Implementation of functions for reading and writing Newick files.
 *
 * For reasons of readability, in this implementation file, the template data types
 * NodeDataType and EdgeDataType are abbreviated using NDT and EDT, respectively.
 *
 * @file
 * @ingroup tree
 */

#include "tree/newick_processor.hpp"

#include "utils/utils.hpp"

namespace genesis {

std::string NewickProcessor::default_leaf_name     = "Leaf Node";
std::string NewickProcessor::default_internal_name = "Internal Node";
std::string NewickProcessor::default_root_name     = "Root Node";

/**
 * @brief If set to true, unnamed nodes are named using one of the default names.
 *
 * The default names can be set using `default_leaf_name`, `default_internal_name` and
 * `default_root_name`. They are used both when parsing and printing a Newick file.
 */
bool        NewickProcessor::use_default_names     = false;

// =============================================================================
//     Parsing
// =============================================================================

bool NewickProcessor::ParseTree (
          NewickLexer::iterator& ct,
    const NewickLexer::iterator& end,
          NewickBroker&          broker
) {
    broker.clear();

    // the node that is currently being populated with data
    NewickBrokerElement* node = nullptr;

    // how deep is the current token nested in the tree?
    int depth = 0;

    // was it closed at some point? we want to avoid a tree like "()();" to be parsed!
    bool closed = false;

    // acts as pointer to previous token
    Lexer::iterator pt = end;

    // store error message. also serves as check whether an error occured
    std::string error = "";

    // --------------------------------------------------------------
    //     Loop over lexer tokens and check if it...
    // --------------------------------------------------------------
    for (; ct != end; pt=ct, ++ct) {
        if (ct->IsUnknown()) {
            error = "Invalid characters at " + ct->at() + ": '" + ct->value() + "'.";
            break;
        }

        // ------------------------------------------------------
        //     is bracket '('  ==>  begin of subtree
        // ------------------------------------------------------
        if (ct->IsBracket("(")) {
            if (pt != end && !(
                pt->IsBracket("(")  || pt->IsOperator(",") || pt->IsComment()
            )) {
                error = "Invalid characters at " + ct->at() + ": '" + ct->value() + "'.";
                break;
            }

            if (closed) {
                error = "Tree was already closed. Cannot reopen it with '(' at " + ct->at() + ".";
                break;
            }

            ++depth;
            continue;
        }

        // ------------------------------------------------------
        //     Prepare for all other tokens.
        // ------------------------------------------------------

        // if we reach this, the previous condition is not fullfilled (otherwise, continue would
        // have been called). so we have a token other than '(', which means we should already
        // be somewhere in the tree (or a comment). check, if that is true.
        if (ct == ct.GetLexer().begin()) {
            if (ct->IsComment()) {
                continue;
            }
            error = "Tree does not start with '(' at " + ct->at() + ".";
            break;
        }

        // if we reached this point in code, this means that ct != begin, so it is not the first
        // iteration in this loop. this means that pt was already set in the loop header (at least
        // once), which means it now points to a valid token.
        assert(pt != end);

        // set up the node that will be filled with data now.
        // if it already exists, this means we are adding more information to it, e.g.
        // a branch length or a tag. so we do not need to create it.
        // however, if this node does not exist, this means we saw a token before that finished an
        // node and pushed it to the stack (either closing bracket or comma), so we need to create a
        // new one here.
        if (!node) {
            node = new NewickBrokerElement();
            node->depth = depth;

            // checks if the new node is a leaf.
            // for this, we need to check whether the previous token was an opening brackt or a
            // comma. however, as comments can appear everywhere, we need to check for the first
            // non-comment-token.
            auto t = pt;
            while (t != pt.GetLexer().begin() && t->IsComment()) {
                --t;
            }
            node->is_leaf = t->IsBracket("(") || t->IsOperator(",");
        }

        // ------------------------------------------------------
        //     is symbol or string  ==>  label
        // ------------------------------------------------------
        if (ct->IsSymbol() || ct->IsString()) {
            if (!(
                pt->IsBracket("(")  || pt->IsBracket(")") ||
                pt->IsOperator(",") || pt->IsComment()
            )) {
                error = "Invalid characters at " + ct->at() + ": '" + ct->value() + "'.";
                break;
            }

            // populate the node
            if (ct->IsSymbol()) {
                // unquoted labels need to turn underscores into space
                node->name = StringReplaceAll(ct->value(), "_", " ");
            } else {
                node->name = ct->value();
            }
            continue;
        }

        // ------------------------------------------------------
        //     is number  ==>  branch length
        // ------------------------------------------------------
        if (ct->IsNumber()) {
            if (!(
                pt->IsBracket("(") || pt->IsBracket(")")  || pt->IsSymbol() || pt->IsString() ||
                pt->IsComment()    || pt->IsOperator(",")
            )) {
                error = "Invalid characters at " + ct->at() + ": '" + ct->value() + "'.";
                break;
            }

            // populate the node
            node->branch_length = std::stod(ct->value());
            continue;
        }

        // ------------------------------------------------------
        //     is tag {}  ==>  tag
        // ------------------------------------------------------
        if (ct->IsTag()) {
            // in some newick extensions, a tag has a semantic meaning that belongs to the
            // current node/edge, thus we need to store it

            // populate the node
            node->tags.push_back(ct->value());
            continue;
        }

        // ------------------------------------------------------
        //     is comment []  ==>  comment
        // ------------------------------------------------------
        if (ct->IsComment()) {
            // in some newick extensions, a comment has a semantic meaning that belongs to the
            // current node/edge, thus we need to store it

            // populate the node
            node->comments.push_back(ct->value());
            continue;
        }

        // ------------------------------------------------------
        //     is comma ','  ==>  next subtree
        // ------------------------------------------------------
        if (ct->IsOperator(",")) {
            if (!(
                pt->IsBracket("(") || pt->IsBracket(")") || pt->IsComment() || pt->IsSymbol() ||
                pt->IsString()     || pt->IsNumber()     || pt->IsTag()     || pt->IsOperator(",")
            )) {
                error = "Invalid ',' at " + ct->at() + ".";
                break;
            }

            // populate the node
            if (node->name.empty() && use_default_names) {
                if (node->is_leaf) {
                    node->name = default_leaf_name;
                } else {
                    node->name = default_internal_name;
                }
            }
            broker.PushTop(node);
            node = nullptr;
            continue;
        }

        // ------------------------------------------------------
        //     is bracket ')'  ==>  end of subtree
        // ------------------------------------------------------
        if (ct->IsBracket(")")) {
            if (depth == 0) {
                error = "Too many ')' at " + ct->at() + ".";
                break;
            }
            if (!(
                pt->IsBracket(")") || pt->IsTag()    || pt->IsComment()     || pt->IsSymbol() ||
                pt->IsString()     || pt->IsNumber() || pt->IsOperator(",")
            )) {
                error = "Invalid ')' at " + ct->at() + ": '" + ct->value() + "'.";
                break;
            }

            // populate the node
            if (node->name.empty() && use_default_names) {
                if (node->is_leaf) {
                    node->name = default_leaf_name;
                } else {
                    node->name = default_internal_name;
                }
            }
            broker.PushTop(node);
            node = nullptr;

            // decrease depth and check if this was the parenthesis that closed the tree
            --depth;
            if (depth == 0) {
                closed = true;
            }
            continue;
        }

        // ------------------------------------------------------
        //     is semicolon ';'  ==>  end of tree
        // ------------------------------------------------------
        if (ct->IsOperator(";")) {
            if (depth != 0) {
                error = "Not enough ')' in tree before closing it with ';' at " + ct->at() + ".";
                break;
            }
            if (!(
                pt->IsBracket(")") || pt->IsSymbol() || pt->IsString() || pt->IsComment() ||
                pt->IsNumber()     || pt->IsTag()
            )) {
                error = "Invalid ';' at " + ct->at() + ": '" + ct->value() + "'.";
                break;
            }

            // populate the node
            if (node->name.empty() && use_default_names) {
                node->name = default_root_name;
            }
            broker.PushTop(node);
            node = nullptr;
            break;
        }

        // If we reach this part of the code, all checkings for token types are done.
        // as we check for every type that NewickLexer yields, and we use a continue or break
        // in each of them, we should never reach this point, unless we forgot a type!
        assert(false);
    }

    if (!error.empty()) {
        LOG_WARN << error;
        return false;
    }

    if (ct == end || !ct->IsOperator(";")) {
        LOG_WARN << "Tree does not finish with a semicolon.";
        return false;
    }

    // Move to the token after the closing semicolon. This is needed for the TreeSet parser.
    ++ct;
    return true;
}

// =============================================================================
//     Printing
// =============================================================================

bool NewickProcessor::print_names          = true;
bool NewickProcessor::print_branch_lengths = false;
bool NewickProcessor::print_comments       = false;
bool NewickProcessor::print_tags           = false;

/**
 * @brief The precision used for printing floating point numbers, particularly the branch_length.
 */
int  NewickProcessor::precision            = 6;

// TODO this is a quick and dirty (=slow) solution...
std::string NewickProcessor::ToStringRec(const NewickBroker& broker, size_t pos)
{
    // check if it is a leaf, stop recursion if so.
    if (broker[pos]->rank() == 0) {
        return ElementToString(broker[pos]);
    }

    // recurse over all children of the current node. while doing so, build a stack of the resulting
    // substrings in reverse order. this is because newick stores the nodes kind of "backwards",
    // by starting at a leaf node instead of the root.
    std::deque<std::string> children;
    for (size_t i = pos + 1; i < broker.size() && broker[i]->depth > broker[pos]->depth; ++i) {
        // skip if not immediate children (those will be called in later recursion steps)
        if (broker[i]->depth > broker[pos]->depth + 1) {
            continue;
        }

        // do the recursion step for this child, add the result to a stack
        children.push_front(ToStringRec(broker, i));
    }

    // build the string by iterating the stack
    std::ostringstream out;
    out << "(";
    for (size_t i = 0; i < children.size(); ++i) {
        if (i>0) {
            out << ",";
        }
        out << children[i];
    }
    out << ")" << ElementToString(broker[pos]);
    return out.str();
}

std::string NewickProcessor::ElementToString(const NewickBrokerElement* bn)
{
    std::string res = "";
    if (print_names) {
        res += StringReplaceAll(bn->name, " ", "_");
    }
    if (print_branch_lengths) {
        res += ":" + ToStringPrecise(bn->branch_length, precision);
    }
    if (print_comments) {
        for (std::string c : bn->comments) {
            res += "[" + c + "]";
        }
    }
    if (print_tags) {
        for (std::string t : bn->tags) {
            res += "{" + t + "}";
        }
    }
    return res;
}

} // namespace genesis