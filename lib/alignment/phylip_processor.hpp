#ifndef GENESIS_ALIGNMENT_PHYLIP_PROCESSOR_H_
#define GENESIS_ALIGNMENT_PHYLIP_PROCESSOR_H_

/**
 * @brief
 *
 * @file
 * @ingroup alignment
 */

#include <assert.h>
#include <string>

#include "utils/lexer.hpp"

namespace genesis {

// =============================================================================
//     Forward declarations
// =============================================================================

class SequenceSet;

// =============================================================================
//     Phylip Lexer
// =============================================================================

class PhylipLexer : public Lexer
{
public:
    PhylipLexer() {
        // we want to include all special chars, in order to allow them in labels.
        // those which do not belong into a sequence are filtered out later.
        set_char_type (LexerTokenType::kSymbol, "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~");

        // set the flags as needed.
        include_whitespace        = true;
        include_comments          = false;
        glue_sign_to_number       = false;
        trim_quotation_marks      = false;
        use_string_escape         = false;
        use_string_doubled_quotes = false;
    }
};

// =============================================================================
//     Phylip Processor
// =============================================================================

/**
 * @brief Processes a Phylip file.
 */
class PhylipProcessor
{
public:

    // ---------------------------------------------------------------------
    //     Parsing
    // ---------------------------------------------------------------------

    static size_t label_length;

    static bool from_file   (const std::string  fn, SequenceSet& sset);
    static bool from_string (const std::string& fs, SequenceSet& sset);

    // ---------------------------------------------------------------------
    //     Printing
    // ---------------------------------------------------------------------

    static bool        to_file   (const SequenceSet& sset, const std::string  fn);
    static void        to_string (const SequenceSet& sset,       std::string& fs);
    static std::string to_string (const SequenceSet& sset);
};

} // namespace genesis

#endif // include guard
