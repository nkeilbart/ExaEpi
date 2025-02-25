/*! @file Utils.H
    \brief Defines the #ExaEpi::Utils namespace
*/

#ifndef UTILS_H_
#define UTILS_H_

#include <AMReX_Geometry.H>
#include <AMReX_IntVect.H>

#include "DemographicData.H"

#include <string>

namespace ExaEpi
{

/*! \brief Structure with various simulation parameters; these are read in
    for the input file provided in the command line.
*/
struct TestParams
{
    amrex::IntVect size;                /*!< domain size; (used only for ic_type = demo)
                                             (see ExaEpi::Utils::get_geometry) */
    int max_grid_size;                  /*!< box size */
    int nsteps;                         /*!< number of simulation steps */
    int plot_int;                       /*!< plot interval (see ExaEpi::IO::writePlotFile) */
    int random_travel_int;              /*!< steps between random travel events
                                             (see AgentContainer::moveRandomTravel) */
    short ic_type;                      /*!< initialization type (see ExaEpi::ICType) */

    /*! Census data filename (DemographicData::InitFromFile)

        The file contents are:
        + The first line of the file contains the number of units.
        + The following lines have the following data:
          + ID (US-wide census tract ID)
          + Population
          + Number of day workers
          + FIPS code
          + Census tract number
          + Numbers of people in age groups: under 5, 5-17, 18-29, 30-64, and 65+
          + Number of households with: 1, 2, 3, 4, 5, 6, and 7 member(s)
    */
    std::string census_filename;

    /*! Worker flow filename (ExaEpi::Initialization::read_workerflow):
        It is a binary file that contains 3 x (number of work patthers) unsigned integer
        data. The 3 integers are: from, to, and the number of workers with this from and to.
        The from and to are the IDs from the first column of the census data file
        (#DemographicData::myID).
    */
    std::string workerflow_filename;

    /*! Initial cases filename (CaseData::InitFromFile):
        The case data file is an ASCII text file with three columns of numbers:
        FIPS code, current number of cases, and cumulative number of cases till date.
    */
    std::string case_filename;

    int aggregated_diag_int;            /*!< interval for writing aggregated diagnostic data
                                             (see: ExaEpi::IO::writeFIPSData) */
    std::string aggregated_diag_prefix; /*!< filename prefix for diagnostic data
                                             (see: ExaEpi::IO::writeFIPSData) */
};

/**
  * \brief enum for the different initial condition options.\n
  *        demo is for an initial power law\n
  *        census reads in census data.\n
  *
  *        Default is demo.
  */
struct ICType {
    enum {
        Demo = 0,   /*!< Initial power law (default) */
        Census = 1  /*!< Census data */
    };
};

/*! \brief Namespace with utility functions */
namespace Utils
{

    void get_test_params (ExaEpi::TestParams& params, const std::string& prefix);

    amrex::Geometry get_geometry (const DemographicData& demo,
                                  const ExaEpi::TestParams& params);

}
}

#endif
