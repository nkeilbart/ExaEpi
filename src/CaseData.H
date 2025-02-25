/*! @file CaseData.H
    \brief #CaseData class definition
*/

#ifndef CASEDATA_H_
#define CASEDATA_H_

#include <AMReX_Vector.H>
#include <AMReX_GpuContainers.H>

#include <string>

/*! \brief Structure containing case data information */
struct CaseData
{
    /*! \brief Null constructor */
    CaseData () {}

    CaseData (const::std::string& fname);

    void InitFromFile (const std::string& fname);

    void Print () const;

    void CopyToDeviceAsync (const amrex::Vector<int>& h_vec, amrex::Gpu::DeviceVector<int>& d_vec);

    void CopyToHostAsync (const amrex::Gpu::DeviceVector<int>& d_vec, amrex::Vector<int>& h_vec);

    void CopyDataToDevice ();

    int N_hubs;                           /*!< number of disease hubs */
    amrex::Vector<int> FIPS_hubs;         /*!< FIPS code of each hub */
    amrex::Vector<int> Size_hubs;         /*!< Num cases in each hub */
    amrex::Vector<int> num_cases;         /*!< Cases in each FIPS code */
    amrex::Vector<int> num_cases2date;    /*!< Cumulative cases in each FIPS code */

    amrex::Gpu::DeviceVector<int> FIPS_hubs_d;         /*!< FIPS code of each hub (GPU) */
    amrex::Gpu::DeviceVector<int> Size_hubs_d;         /*!< Num cases in each hub (GPU) */
    amrex::Gpu::DeviceVector<int> num_cases_d;         /*!< Cases in each FIPS (GPU) */
    amrex::Gpu::DeviceVector<int> num_cases2date_d;    /*!< Cumulative cases in each FIPS (GPU) */
};

#endif
