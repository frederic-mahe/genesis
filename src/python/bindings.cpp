/**
 * @brief Definition of the genesis Python module.
 *
 * @file
 * @ingroup python
 */

#include <boost/python.hpp>

// =============================================================================
//     Forward declarations of all exported classes
// =============================================================================

    // -------------------------------------------
    //     Placement
    // -------------------------------------------

void BoostPythonExport_JplaceProcessor();
void BoostPythonExport_PlacementMap();
void BoostPythonExport_PlacementTree();

    // -------------------------------------------
    //     Tree
    // -------------------------------------------

void BoostPythonExport_NewickProcessor();
//~ void BoostPythonExport_TreeSet();

    // -------------------------------------------
    //     Utils
    // -------------------------------------------

void BoostPythonExport_Bitvector();
void BoostPythonExport_Logging();

// =============================================================================
//     Boost Python Module
// =============================================================================

BOOST_PYTHON_MODULE(genesis)
{
    // show genesis docstrings, python signature, but not c++ signature
    boost::python::docstring_options doc_options(true, true, false);

    // -------------------------------------------
    //     Placement
    // -------------------------------------------

    BoostPythonExport_JplaceProcessor();
    BoostPythonExport_PlacementMap();
    BoostPythonExport_PlacementTree();

    // -------------------------------------------
    //     Tree
    // -------------------------------------------

    BoostPythonExport_NewickProcessor();
    //~ BoostPythonExport_TreeSet();

    // -------------------------------------------
    //     Utils
    // -------------------------------------------

    BoostPythonExport_Bitvector();
    BoostPythonExport_Logging();

}