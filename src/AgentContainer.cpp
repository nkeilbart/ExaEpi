/*! @file AgentContainer.cpp
    \brief Function implementations for #AgentContainer class
*/

#include "AgentContainer.H"

using namespace amrex;

namespace {

    /*! \brief Shuffle the elements of a given vector */
    void randomShuffle (std::vector<int>& vec /*!< Vector to be shuffled */)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(vec.begin(), vec.end(), g);
    }

    /*! \brief
    */
    void compute_initial_distribution (amrex::Vector<int>& cell_pops, /*!< */
                                       amrex::Vector<int>& cell_indices, /*!< */
                                       int ncell /*!< */)
    {
        BL_PROFILE("compute_initial_distribution");

        AMREX_ALWAYS_ASSERT(ncell == 3000); // hard-coded right now

        cell_pops.resize(0);
        cell_pops.resize(ncell*ncell, -1);

        // we compute the initial distribution on Rank 0 and broadcast to all ranks
        if (ParallelDescriptor::IOProcessor())
        {
            int num_pop_bins = 1000;
            amrex::Real log_min_pop = 1.062;
            amrex::Real log_max_pop = 4.0;
            amrex::Vector<amrex::Real> cell_pop_bins_r(num_pop_bins);
            amrex::Vector<amrex::Real> num_cells_per_bin_r(num_pop_bins);

            for (int i = 0; i < cell_pop_bins_r.size(); ++i) {
                cell_pop_bins_r[i] = std::pow(10.0,
                    log_min_pop + i*(log_max_pop - log_min_pop)/(num_pop_bins-1));
                num_cells_per_bin_r[i] = std::pow(cell_pop_bins_r[i], -1.5);
            }

            amrex::Real norm = 0;
            for (int i = 0; i < num_cells_per_bin_r.size(); ++i) {
                norm += num_cells_per_bin_r[i];
            }

            amrex::Vector<int> cell_pop_bins(num_pop_bins);
            amrex::Vector<int> num_cells_per_bin(num_pop_bins);
            for (int i = 0; i < num_cells_per_bin.size(); ++i) {
                num_cells_per_bin_r[i] *= (ncell*ncell/norm);
                num_cells_per_bin[i] = static_cast<int>(std::round(num_cells_per_bin_r[i]));
                cell_pop_bins[i] = static_cast<int>(std::round(cell_pop_bins_r[i]));
            }

            int total_cells = 0;
            for (int i = 0; i < num_cells_per_bin.size(); ++i) {
                total_cells += num_cells_per_bin[i];
            }
            num_cells_per_bin[0] += (ncell*ncell - total_cells);

            std::vector<int> perm(ncell*ncell);
            std::iota(perm.begin(), perm.end(), 0);
            randomShuffle(perm);

            Vector<int> offsets(num_pop_bins+1);
            offsets[0] = 0;
            for (int i = 1; i < num_pop_bins+1; ++i) {
                offsets[i] = offsets[i-1] + num_cells_per_bin[i-1];
            }

            for (int i = 0; i < num_pop_bins; ++i) {
                for (int j = offsets[i]; j < offsets[i+1]; ++j) {
                    cell_pops[perm[j]] = cell_pop_bins[i];
                }
            }

            int total_agents = 0;
            for (int i = 0; i < cell_pops.size(); ++i) {
                total_agents += cell_pops[i];
            }
            amrex::Print() << "Total number of agents: " << total_agents << "\n";

            amrex::Print() << "Splitting up population into interior and border\n";
            // we now have a list of populations for each cell. We want 1/3
            // of the population to be within 200 cells of the border. We
            // maintain two separate lists, one for the interior, one for the exterior
            int interior_size = 2600*2600;
            int border_size = ncell*ncell - interior_size;

            // First we sort the vector of cell pops
            std::sort(cell_pops.begin(), cell_pops.end());
            amrex::Real border_pop = 0;
            int i = cell_pops.size()-1;
            std::vector<int> border_ids;
            std::vector<int> interior_ids;
            while ((border_pop < 100e6) && (i >= 0)) {
                amrex::Real pop = cell_pops[i];
                if (amrex::Random() < 0.5) {
                    border_ids.push_back(i);
                    border_pop += pop;
                }
                else {
                    interior_ids.push_back(i);
                }
                --i;
            }

            while (interior_ids.size() < static_cast<std::size_t>(interior_size)) {
                interior_ids.push_back(i);
                --i;
            }

            while (i >= 0) {
                amrex::Real pop = cell_pops[i];
                border_pop += pop;
                border_ids.push_back(i);
                --i;
            }

            // if these conditions are not met, then something has gone wrong with the border pop
            AMREX_ALWAYS_ASSERT(i == -1);
            AMREX_ALWAYS_ASSERT(interior_ids.size() == static_cast<std::size_t>(interior_size));
            AMREX_ALWAYS_ASSERT(border_ids.size() == static_cast<std::size_t>(border_size));

            amrex::Print() << "Population within 200 cells of border is " << border_pop << "\n";

            randomShuffle(border_ids);
            randomShuffle(interior_ids);

            for (int cell_id = 0; cell_id < ncell*ncell; ++cell_id) {
                int idx = cell_id % ncell;
                int idy = cell_id / ncell;
                if ((idx < 200) || (idx >= 2800) || (idy < 200) || (idy >= 2800)) {
                    cell_indices.push_back(border_ids.back());
                    border_ids.pop_back();
                } else {
                    cell_indices.push_back(interior_ids.back());
                    interior_ids.pop_back();
                }
            }
            AMREX_ALWAYS_ASSERT(interior_ids.size() == 0);
            AMREX_ALWAYS_ASSERT(border_ids.size() == 0);
        } else {
            cell_indices.resize(0);
            cell_indices.resize(ncell*ncell);
        }

        // Broadcast
        ParallelDescriptor::Bcast(&cell_pops[0], cell_pops.size(),
                                  ParallelDescriptor::IOProcessorNumber());
        ParallelDescriptor::Bcast(&cell_indices[0], cell_indices.size(),
                                  ParallelDescriptor::IOProcessorNumber());
    }
}

/*! \brief Initialize agents for ExaEpi::ICType::Demo */
void AgentContainer::initAgentsDemo (iMultiFab& /*num_residents*/,
                                     iMultiFab& /*unit_mf*/,
                                     iMultiFab& /*FIPS_mf*/,
                                     iMultiFab& /*comm_mf*/,
                                     DemographicData& /*demo*/)
{
    BL_PROFILE("AgentContainer::initAgentsDemo");

    int ncell = 3000;
    Vector<int> cell_pops;
    Vector<int> cell_indices;

    compute_initial_distribution(cell_pops, cell_indices, ncell);

    // now each rank will only actually add a subset of the particles
    int ibegin, iend;
    {
        int myproc = ParallelDescriptor::MyProc();
        int nprocs = ParallelDescriptor::NProcs();
        int navg = ncell*ncell/nprocs;
        int nleft = ncell*ncell - navg * nprocs;
        if (myproc < nleft) {
            ibegin = myproc*(navg+1);
            iend = ibegin + navg+1;
        } else {
            ibegin = myproc*navg + nleft;
            iend = ibegin + navg;
        }
    }
    std::size_t ncell_this_rank = iend-ibegin;

    std::size_t np_this_rank = 0;
    for (int i = 0; i < ncell*ncell; ++i) {
        if ((i < ibegin) || (i >= iend)) {
            cell_pops[cell_indices[i]] = 0;
        } else {
            np_this_rank += cell_pops[cell_indices[i]];
        }
    }

    // copy data to GPU
    amrex::Gpu::DeviceVector<int> cell_pops_d(cell_pops.size());
    amrex::Gpu::DeviceVector<int> cell_offsets_d(cell_pops.size()+1);
    Gpu::copy(Gpu::hostToDevice, cell_pops.begin(), cell_pops.end(),
              cell_pops_d.begin());
    Gpu::exclusive_scan(cell_pops_d.begin(), cell_pops_d.end(), cell_offsets_d.begin());

    amrex::Gpu::DeviceVector<int> cell_indices_d(cell_indices.size());
    Gpu::copy(Gpu::hostToDevice, cell_indices.begin(), cell_indices.end(), cell_indices_d.begin());

    // Fill in particle data in each cell
    auto& ptile = DefineAndReturnParticleTile(0, 0, 0);
    ptile.resize(np_this_rank);

    auto& soa   = ptile.GetStructOfArrays();
    auto& aos   = ptile.GetArrayOfStructs();
    auto pstruct_ptr = aos().data();
    auto status_ptr = soa.GetIntData(IntIdx::status).data();
    auto strain_ptr = soa.GetIntData(IntIdx::strain).data();
    auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();

    auto cell_offsets_ptr = cell_offsets_d.data();
    auto cell_indices_ptr = cell_indices_d.data();

    amrex::ParallelForRNG( ncell_this_rank,
    [=] AMREX_GPU_DEVICE (int i_this_rank, RandomEngine const& engine) noexcept
    {
        int cell_id = i_this_rank + ibegin;
        int ind = cell_indices_ptr[cell_id];

        int cell_start = cell_offsets_ptr[ind];
        int cell_stop = cell_offsets_ptr[ind+1];

        int idx = cell_id % ncell;
        int idy = cell_id / ncell;

        for (int i = cell_start; i < cell_stop; ++i) {
            auto& p = pstruct_ptr[i];
            p.pos(0) = idx + 0.5;
            p.pos(1) = idy + 0.5;
            p.id() = i;
            p.cpu() = 0;

            counter_ptr[i] = 0.0;
            strain_ptr[i] = 0;

            if (amrex::Random(engine) < 1e-6) {
                status_ptr[i] = 1;
                if (amrex::Random(engine) < 0.3) {
                    strain_ptr[i] = 1;
                }
            }
        }
    });

    amrex::Print() << "Initial Redistribute... ";

    Redistribute();

    amrex::Print() << "... finished initialization\n";
}

/*! \brief Initialize agents for ExaEpi::ICType::Census

 *  + Define and allocate the following integer MultiFabs:
 *    + num_families: number of families; has 7 components, each component is the
 *      number of families of size (component+1)
 *    + fam_offsets: offset array for each family (i.e., each component of each grid cell), where the
 *      offset is the total number of people before this family while iterating over the grid.
 *    + fam_id: ID array for each family ()i.e., each component of each grid cell, where the ID is the
 *      total number of families before this family while iterating over the grid.
 *  + At each grid cell in each box/tile on each processor:
 *    + Set community number.
 *    + Find unit number for this community; specify that a part of this unit is on this processor;
 *      set unit number, FIPS code, and census tract number at this grid cell (community).
 *    + Set community size: 2000 people, unless this is the last community of a unit, in which case
 *      the remaining people if > 1000 (else 0).
 *    + Compute cumulative distribution (on a scale of 0-1000) of household size ranging from 1 to 7:
 *      initialize with default distributions, then compute from census data if available.
 *    + For each person in this community, generate a random integer between 0 and 1000; based on its
 *      value, assign this person to a household of a certain size (1-7) based on the cumulative
 *      distributions above.
 *  + Compute total number of agents (people), family offsets and IDs over the box/tile.
 *  + Allocate particle container AoS and SoA arrays for the computed number of agents.
 *  + At each grid cell in each box/tile on each processor, and for each component (where component
 *    corresponds to family size):
 *    + Compute percentage of school age kids (kids of age 5-17 as a fraction of total kids - under 5
 *      plus 5-17), if available in census data or set to default (76%).
 *    + For each agent at this grid cell and family size (component):
 *      + Find age group by generating a random integer (0-100) and using default age distributions.
 *        Look at code to see the algorithm for family size > 1.
 *      + Set agent position at the center of this grid cell.
 *      + Initialize status and day counters.
 *      + Set age group and family ID.
 *      + Set home location to current grid cell.
 *      + Initialize work location to current grid cell. Actual work location is set in
 *        ExaEpi::read_workerflow().
 *      + Set neighborhood and work neighborhood values. Actual work neighborhood is set
 *        in ExaEpi::read_workerflow().
 *      + Initialize workgroup to 0. It is set in ExaEpi::read_workerflow().
 *      + If age group is 5-17, assign a school based on neighborhood (#assign_school).
 *  + Copy everything to GPU device.
*/
void AgentContainer::initAgentsCensus (iMultiFab& num_residents,    /*!< Number of residents in each community (grid cell);
                                                                         component 0: age under 5,
                                                                         component 1: age group 5-17,
                                                                         component 2: age group 18-29,
                                                                         component 3: age group 30-64,
                                                                         component 4: age group 65+,
                                                                         component 4: total. */
                                       iMultiFab& unit_mf,          /*!< Unit number of each community */
                                       iMultiFab& FIPS_mf,          /*!< FIPS code (component 0) and
                                                                         census tract number (component 1)
                                                                         of each community */
                                       iMultiFab& comm_mf,          /*!< Community number */
                                       DemographicData& demo        /*!< Demographic data */ )
{
    BL_PROFILE("initAgentsCensus");

    using AgentType = ParticleType;

    const Box& domain = Geom(0).Domain();

    num_residents.setVal(0);
    unit_mf.setVal(-1);
    FIPS_mf.setVal(-1);
    comm_mf.setVal(-1);

    iMultiFab num_families(num_residents.boxArray(), num_residents.DistributionMap(), 7, 0);
    iMultiFab fam_offsets (num_residents.boxArray(), num_residents.DistributionMap(), 7, 0);
    iMultiFab fam_id (num_residents.boxArray(), num_residents.DistributionMap(), 7, 0);
    num_families.setVal(0);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(unit_mf, TilingIfNotGPU()); mfi.isValid(); ++mfi)
    {
        auto unit_arr = unit_mf[mfi].array();
        auto FIPS_arr = FIPS_mf[mfi].array();
        auto comm_arr = comm_mf[mfi].array();
        auto nf_arr = num_families[mfi].array();
        auto nr_arr = num_residents[mfi].array();

        auto unit_on_proc = demo.Unit_on_proc_d.data();
        auto Start = demo.Start_d.data();
        auto FIPS = demo.FIPS_d.data();
        auto Tract = demo.Tract_d.data();
        auto Population = demo.Population_d.data();

        auto H1 = demo.H1_d.data();
        auto H2 = demo.H2_d.data();
        auto H3 = demo.H3_d.data();
        auto H4 = demo.H4_d.data();
        auto H5 = demo.H5_d.data();
        auto H6 = demo.H6_d.data();
        auto H7 = demo.H7_d.data();

        auto N5  = demo.N5_d.data();
        auto N17 = demo.N17_d.data();
        //auto N29 = demo.N29_d.data();
        //auto N64 = demo.N64_d.data();
        //auto N65plus = demo.N65plus_d.data();

        auto Ncommunity = demo.Ncommunity;

        auto bx = mfi.tilebox();
        amrex::ParallelForRNG(bx, [=] AMREX_GPU_DEVICE (int i, int j, int k, amrex::RandomEngine const& engine) noexcept
        {
            int community = (int) domain.index(IntVect(AMREX_D_DECL(i, j, k)));
            if (community >= Ncommunity) { return; }
            comm_arr(i, j, k) = community;

            int unit = 0;
            while (community >= Start[unit+1]) { unit++; }
            unit_on_proc[unit] = 1;
            unit_arr(i, j, k) = unit;
            FIPS_arr(i, j, k, 0) = FIPS[unit];
            FIPS_arr(i, j, k, 1) = Tract[unit];

            int community_size;
            if (Population[unit] < (1000 + 2000*(community - Start[unit]))) {
                community_size = 0;  /* Don't set up any residents; workgroup-only */
            }
            else {
                community_size = 2000;   /* Standard 2000-person community */
            }

            int p_hh[7] = {330, 670, 800, 900, 970, 990, 1000};
            int num_hh = H1[unit] + H2[unit] + H3[unit] +
                H4[unit] + H5[unit] + H6[unit] + H7[unit];
            if (num_hh) {
                p_hh[0] = 1000 * H1[unit] / num_hh;
                p_hh[1] = 1000* (H1[unit] + H2[unit]) / num_hh;
                p_hh[2] = 1000* (H1[unit] + H2[unit] + H3[unit]) / num_hh;
                p_hh[3] = 1000* (H1[unit] + H2[unit] + H3[unit] + H4[unit]) / num_hh;
                p_hh[4] = 1000* (H1[unit] + H2[unit] + H3[unit] +
                                 H4[unit] + H5[unit]) / num_hh;
                p_hh[5] = 1000* (H1[unit] + H2[unit] + H3[unit] +
                                 H4[unit] + H5[unit] + H6[unit]) / num_hh;
                p_hh[6] = 1000;
            }

            int npeople = 0;
            while (npeople < community_size + 1) {
                int il  = amrex::Random_int(1000, engine);

                int family_size = 1;
                while (il > p_hh[family_size]) { ++family_size; }
                AMREX_ASSERT(family_size > 0);
                AMREX_ASSERT(family_size <= 7);

                nf_arr(i, j, k, family_size-1) += 1;
                npeople += family_size;
            }

            AMREX_ASSERT(npeople == nf_arr(i, j, k, 0) +
                         2*nf_arr(i, j, k, 1) +
                         3*nf_arr(i, j, k, 2) +
                         4*nf_arr(i, j, k, 3) +
                         5*nf_arr(i, j, k, 4) +
                         6*nf_arr(i, j, k, 5) +
                         7*nf_arr(i, j, k, 6));

            nr_arr(i, j, k, 5) = npeople;
        });

        int nagents;
        int ncomp = num_families[mfi].nComp();
        int ncell = num_families[mfi].numPts();
        {
            BL_PROFILE("setPopulationCounts_prefixsum")
            const int* in = num_families[mfi].dataPtr();
            int* out = fam_offsets[mfi].dataPtr();
            nagents = Scan::PrefixSum<int>(ncomp*ncell,
                            [=] AMREX_GPU_DEVICE (int i) -> int {
                                int comp = i / ncell;
                                return (comp+1)*in[i];
                            },
                            [=] AMREX_GPU_DEVICE (int i, int const& x) { out[i] = x; },
                                               Scan::Type::exclusive, Scan::retSum);
        }
        {
            BL_PROFILE("setFamily_id_prefixsum")
            const int* in = num_families[mfi].dataPtr();
            int* out = fam_id[mfi].dataPtr();
            Scan::PrefixSum<int>(ncomp*ncell,
                                 [=] AMREX_GPU_DEVICE (int i) -> int {
                                     return in[i];
                                 },
                                 [=] AMREX_GPU_DEVICE (int i, int const& x) { out[i] = x; },
                                 Scan::Type::exclusive, Scan::retSum);
        }

        auto offset_arr = fam_offsets[mfi].array();
        auto fam_id_arr = fam_id[mfi].array();
        auto& agents_tile = GetParticles(0)[std::make_pair(mfi.index(),mfi.LocalTileIndex())];
        agents_tile.resize(nagents);
        auto aos = &agents_tile.GetArrayOfStructs()[0];
        auto& soa = agents_tile.GetStructOfArrays();

        auto status_ptr = soa.GetIntData(IntIdx::status).data();
        auto age_group_ptr = soa.GetIntData(IntIdx::age_group).data();
        auto family_ptr = soa.GetIntData(IntIdx::family).data();
        auto home_i_ptr = soa.GetIntData(IntIdx::home_i).data();
        auto home_j_ptr = soa.GetIntData(IntIdx::home_j).data();
        auto work_i_ptr = soa.GetIntData(IntIdx::work_i).data();
        auto work_j_ptr = soa.GetIntData(IntIdx::work_j).data();
        auto nborhood_ptr = soa.GetIntData(IntIdx::nborhood).data();
        auto school_ptr = soa.GetIntData(IntIdx::school).data();
        auto workgroup_ptr = soa.GetIntData(IntIdx::workgroup).data();
        auto work_nborhood_ptr = soa.GetIntData(IntIdx::work_nborhood).data();

        auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();
        auto dx = ParticleGeom(0).CellSizeArray();
        auto my_proc = ParallelDescriptor::MyProc();

        Long pid;
#ifdef AMREX_USE_OMP
#pragma omp critical (init_agents_nextid)
#endif
        {
            pid = AgentType::NextID();
            AgentType::NextID(pid+nagents);
        }
        AMREX_ALWAYS_ASSERT_WITH_MESSAGE(
            static_cast<Long>(pid + nagents) < LastParticleID,
            "Error: overflow on agent id numbers!");

        amrex::ParallelForRNG(bx, ncomp, [=] AMREX_GPU_DEVICE (int i, int j, int k, int n, amrex::RandomEngine const& engine) noexcept
        {
            int nf = nf_arr(i, j, k, n);
            if (nf == 0) return;

            int unit = unit_arr(i, j, k);
            int community = comm_arr(i, j, k);
            int family_id = fam_id_arr(i, j, k, n);
            int family_size = n + 1;
            int num_to_add = family_size * nf;

            int community_size;
            if (Population[unit] < (1000 + 2000*(community - Start[unit]))) {
                community_size = 0;  /* Don't set up any residents; workgroup-only */
            }
            else {
                community_size = 2000;   /* Standard 2000-person community */
            }

            int p_schoolage = 0;
            if (community_size) {  // Only bother for residential communities
                if (N5[unit] + N17[unit]) {
                    p_schoolage = 100*N17[unit] / (N5[unit] + N17[unit]);
                }
                else {
                    p_schoolage = 76;
                }
            }

            int start = offset_arr(i, j, k, n);
            for (int ip = start; ip < start + num_to_add; ++ip) {
                auto& agent = aos[ip];
                int il2 = amrex::Random_int(100, engine);
                int nborhood = amrex::Random_int(4, engine);
                int age_group = -1;

                if (family_size == 1) {
                    if (il2 < 28) { age_group = 4; }      /* single adult age 65+   */
                    else if (il2 < 68) { age_group = 3; } /* age 30-64 (ASSUME 40%) */
                    else { age_group = 2; }               /* single adult age 19-29 */
                    nr_arr(i, j, k, age_group) += 1;
                } else if (family_size == 2) {
                    if (il2 == 0) {
                        /* 1% probability of one parent + one child */
                        int il3 = amrex::Random_int(100, engine);
                        if (il3 < 2) { age_group = 4; }        /* one parent, age 65+ */
                        else if (il3 < 62) { age_group = 3; }  /* one parent 30-64 (ASSUME 60%) */
                        else { age_group = 2; }                /* one parent 19-29 */
                        nr_arr(i, j, k, age_group) += 1;
                        if (((int) amrex::Random_int(100, engine)) < p_schoolage) {
                            age_group = 1; /* 22.0% of total population ages 5-18 */
                        } else {
                            age_group = 0;   /* 6.8% of total population ages 0-4 */
                        }
                        nr_arr(i, j, k, age_group) += 1;
                    } else {
                        /* 2 adults, 28% over 65 (ASSUME both same age group) */
                        if (il2 < 28) { age_group = 4; }      /* single adult age 65+ */
                        else if (il2 < 68) { age_group = 3; } /* age 30-64 (ASSUME 40%) */
                        else { age_group = 2; }               /* single adult age 19-29 */
                        nr_arr(i, j, k, age_group) += 2;
                    }
                }

                if (family_size > 2) {
                    /* ASSUME 2 adults, of the same age group */
                    if (il2 < 2) { age_group = 4; }  /* parents are age 65+ */
                    else if (il2 < 62) { age_group = 3; }  /* parents 30-64 (ASSUME 60%) */
                    else { age_group = 2; }  /* parents 19-29 */
                    nr_arr(i, j, k, age_group) += 2;

                    /* Now pick the children's age groups */
                    for (int nc = 2; nc < family_size; ++nc) {
                        if (((int) amrex::Random_int(100, engine)) < p_schoolage) {
                            age_group = 1; /* 22.0% of total population ages 5-18 */
                        } else {
                            age_group = 0;   /* 6.8% of total population ages 0-4 */
                        }
                        nr_arr(i, j, k, age_group) += 1;
                    }
                }

                agent.pos(0) = (i + 0.5)*dx[0];
                agent.pos(1) = (j + 0.5)*dx[1];
                agent.id()  = pid+ip;
                agent.cpu() = my_proc;

                status_ptr[ip] = 0;
                counter_ptr[ip] = 0.0;
                age_group_ptr[ip] = age_group;
                family_ptr[ip] = family_id++;
                home_i_ptr[ip] = i;
                home_j_ptr[ip] = j;
                work_i_ptr[ip] = i;
                work_j_ptr[ip] = j;
                nborhood_ptr[ip] = nborhood;
                work_nborhood_ptr[ip] = 5*nborhood;
                workgroup_ptr[ip] = 0;

                if (age_group == 0) {
                    school_ptr[ip] = 5; // note - need to handle playgroups
                } else if (age_group == 1) {
                    school_ptr[ip] = assign_school(nborhood, engine);
                } else{
                    school_ptr[ip] = -1;
                }
            }
        });
    }

    demo.CopyToHostAsync(demo.Unit_on_proc_d, demo.Unit_on_proc);
    amrex::Gpu::streamSynchronize();
}

/*! \brief Send agents on a random walk around the neighborhood

    For each agent, set its position to a random one near its current position
*/
void AgentContainer::moveAgentsRandomWalk ()
{
    BL_PROFILE("AgentContainer::moveAgentsRandomWalk");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        const auto dx = Geom(lev).CellSizeArray();
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& aos   = ptile.GetArrayOfStructs();
            ParticleType* pstruct = &(aos[0]);
            const size_t np = aos.numParticles();

            amrex::ParallelForRNG( np,
            [=] AMREX_GPU_DEVICE (int i, RandomEngine const& engine) noexcept
            {
                ParticleType& p = pstruct[i];
                p.pos(0) += static_cast<ParticleReal> ((2*amrex::Random(engine)-1)*dx[0]);
                p.pos(1) += static_cast<ParticleReal> ((2*amrex::Random(engine)-1)*dx[1]);
            });
        }
    }
}

/*! \brief Move agents to work

    For each agent, set its position to the work community (IntIdx::work_i, IntIdx::work_j)
*/
void AgentContainer::moveAgentsToWork ()
{
    BL_PROFILE("AgentContainer::moveAgentsToWork");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        const auto dx = Geom(lev).CellSizeArray();
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& aos   = ptile.GetArrayOfStructs();
            ParticleType* pstruct = &(aos[0]);
            const size_t np = aos.numParticles();

            auto& soa = ptile.GetStructOfArrays();
            auto work_i_ptr = soa.GetIntData(IntIdx::work_i).data();
            auto work_j_ptr = soa.GetIntData(IntIdx::work_j).data();

            amrex::ParallelFor( np,
            [=] AMREX_GPU_DEVICE (int ip) noexcept
            {
                ParticleType& p = pstruct[ip];
                p.pos(0) = (work_i_ptr[ip] + 0.5)*dx[0];
                p.pos(1) = (work_j_ptr[ip] + 0.5)*dx[1];
            });
        }
    }
}

/*! \brief Move agents to home

    For each agent, set its position to the home community (IntIdx::home_i, IntIdx::home_j)
*/
void AgentContainer::moveAgentsToHome ()
{
    BL_PROFILE("AgentContainer::moveAgentsToHome");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        const auto dx = Geom(lev).CellSizeArray();
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& aos   = ptile.GetArrayOfStructs();
            ParticleType* pstruct = &(aos[0]);
            const size_t np = aos.numParticles();

            auto& soa = ptile.GetStructOfArrays();
            auto home_i_ptr = soa.GetIntData(IntIdx::home_i).data();
            auto home_j_ptr = soa.GetIntData(IntIdx::home_j).data();

            amrex::ParallelFor( np,
            [=] AMREX_GPU_DEVICE (int ip) noexcept
            {
                ParticleType& p = pstruct[ip];
                p.pos(0) = (home_i_ptr[ip] + 0.5)*dx[0];
                p.pos(1) = (home_j_ptr[ip] + 0.5)*dx[1];
            });
        }
    }
}

/*! \brief Move agents randomly

    For each agent, set its position to a random location with a probabilty of 0.01%
*/
void AgentContainer::moveRandomTravel ()
{
    BL_PROFILE("AgentContainer::moveRandomTravel");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& aos   = ptile.GetArrayOfStructs();
            ParticleType* pstruct = &(aos[0]);
            const size_t np = aos.numParticles();

            amrex::ParallelForRNG( np,
            [=] AMREX_GPU_DEVICE (int i, RandomEngine const& engine) noexcept
            {
                ParticleType& p = pstruct[i];

                if (amrex::Random(engine) < 0.0001) {
                    p.pos(0) = 3000*amrex::Random(engine);
                    p.pos(1) = 3000*amrex::Random(engine);
                }
            });
        }
    }
}

/*! \brief Updates disease status of each agent at a given step and also updates a MultiFab
    that tracks disease statistics (hospitalization, ICU, ventilator, and death) in a community.

    At a given step, update the disease status of each agent based on the following overall logic:
    + If agent status is #Status::never or #Status::susceptible, do nothing
    + If agent status is #Status::infected, then
      + Increment its counter by 1 day
      + If counter is within incubation period (#DiseaseParm::incubation_length days), do nothing more
      + Else on day #DiseaseParm::incubation_length, use hospitalization probabilities (by age group)
        to decide if agent is hospitalized. If yes, use age group to set hospital timer. Also, use
        age-group-wise probabilities to move agent to ICU and then to ventilator. Adjust timer
        accordingly.
      + Update the community-wise disease stats tracker MultiFab according to hospitalization/ICU/vent
        status (using the agent's home community)
      + Else (beyond 3 days), count down hospital timer if agent is hospitalized. At end of hospital
        stay, determine if agent is #Status dead or #Status::immune. For non-hospitalized agents,
        set them to #Status::immune after #DiseaseParm::incubation_length +
        #DiseaseParm::infectious_length days.

    The input argument is a MultiFab with 4 components corresponding to "hospitalizations", "ICU",
    "ventilator", and "death". It contains the cumulative totals of these quantities for each
    community as the simulation progresses.
*/
void AgentContainer::updateStatus (MultiFab& disease_stats /*!< Community-wise disease stats tracker */)
{
    BL_PROFILE("AgentContainer::updateStatus");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& soa   = ptile.GetStructOfArrays();
            const auto np = ptile.numParticles();
            auto status_ptr = soa.GetIntData(IntIdx::status).data();
            auto age_group_ptr = soa.GetIntData(IntIdx::age_group).data();
            auto home_i_ptr = soa.GetIntData(IntIdx::home_i).data();
            auto home_j_ptr = soa.GetIntData(IntIdx::home_j).data();
            auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();
            auto timer_ptr = soa.GetRealData(RealIdx::treatment_timer).data();
            auto prob_ptr = soa.GetRealData(RealIdx::prob).data();
            auto incubation_period_ptr = soa.GetRealData(RealIdx::incubation_period).data();
            auto infectious_period_ptr = soa.GetRealData(RealIdx::infectious_period).data();

            auto ds_arr = disease_stats[mfi].array();

            struct DiseaseStats
            {
                enum {
                    hospitalization = 0,
                    ICU,
                    ventilator,
                    death
                };
            };

            // Track hospitalization, ICU, ventilator, and fatalities
            Real CHR[] = {.0104, .0104, .070, .28, 1.0};  // sick -> hospital probabilities
            Real CIC[] = {.24, .24, .24, .36, .35};      // hospital -> ICU probabilities
            Real CVE[] = {.12, .12, .12, .22, .22};      // ICU -> ventilator probabilities
            Real CVF[] = {.20, .20, .20, 0.45, 1.26};    // ventilator -> dead probilities
            amrex::ParallelForRNG( np,
                                   [=] AMREX_GPU_DEVICE (int i, amrex::RandomEngine const& engine) noexcept
            {
                prob_ptr[i] = 1.0;
                if ( status_ptr[i] == Status::never ||
                     status_ptr[i] == Status::susceptible ) {
                    return;
                }
                else if (status_ptr[i] == Status::infected) {
                    counter_ptr[i] += 1;
                    if (counter_ptr[i] < incubation_period_ptr[i]) {
                        // incubation phase
                        return;
                    }
                    if (counter_ptr[i] == amrex::Math::ceil(incubation_period_ptr[i])) {
                        // decide if hospitalized
                        Real p_hosp = CHR[age_group_ptr[i]];
                        if (amrex::Random(engine) < p_hosp) {
                            if ((age_group_ptr[i]) < 3) {  // age groups 0-4, 5-18, 19-29
                                timer_ptr[i] = 3;  // Ages 0-49 hospitalized for 3.1 days
                            }
                            else if (age_group_ptr[i] == 4) {
                                timer_ptr[i] = 7;  // Age 65+ hospitalized for 6.5 days
                            }
                            else if (amrex::Random(engine) < 0.57) {
                                timer_ptr[i] = 3;  // Proportion of 30-64 that is under 50
                            }
                            else {
                                timer_ptr[i] = 8;  // Age 50-64 hospitalized for 7.8 days
                            }
                            amrex::Gpu::Atomic::AddNoRet(
                                &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                        DiseaseStats::hospitalization), 1.0_rt);
                            if (amrex::Random(engine) < CIC[age_group_ptr[i]]) {
                                //std::printf("putting h in icu \n");
                                timer_ptr[i] += 10;  // move to ICU
                                amrex::Gpu::Atomic::AddNoRet(
                                    &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                            DiseaseStats::ICU), 1.0_rt);
                                if (amrex::Random(engine) < CVE[age_group_ptr[i]]) {
                                    //std::printf("putting icu on v \n");
                                    amrex::Gpu::Atomic::AddNoRet(
                                    &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                            DiseaseStats::ventilator), 1.0_rt);
                                    timer_ptr[i] += 10;  // put on ventilator
                                }
                            }
                        }
                    } else {
                        if (timer_ptr[i] > 0.0) {
                            // do hospital things
                            timer_ptr[i] -= 1.0;
                            if (timer_ptr[i] == 0) {
                                if (CVF[age_group_ptr[i]] > 2.0) {
                                    if (amrex::Random(engine) < (CVF[age_group_ptr[i]] - 2.0)) {
                                        amrex::Gpu::Atomic::AddNoRet(
                                            &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                    DiseaseStats::death), 1.0_rt);
                                        status_ptr[i] = Status::dead;
                                        //pstruct_ptr[i].id() = -pstruct_ptr[i].id();
                                    }
                                }
                                amrex::Gpu::Atomic::AddNoRet(
                                                             &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                                     DiseaseStats::hospitalization), -1.0_rt);
                                if (status_ptr[i] != Status::dead) {
                                    status_ptr[i] = Status::immune;  // If alive, hospitalized patient recovers
                                }
                            }
                            if (timer_ptr[i] == 10) {
                                if (CVF[age_group_ptr[i]] > 1.0) {
                                    if (amrex::Random(engine) < (CVF[age_group_ptr[i]] - 1.0)) {
                                        amrex::Gpu::Atomic::AddNoRet(
                                            &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                    DiseaseStats::death), 1.0_rt);
                                        status_ptr[i] = Status::dead;
                                        //pstruct_ptr[i].id() = -pstruct_ptr[i].id();
                                    }
                                }
                                amrex::Gpu::Atomic::AddNoRet(
                                                             &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                                     DiseaseStats::ICU), -1.0_rt);
                                if (status_ptr[i] != Status::dead) {
                                    status_ptr[i] = Status::immune;  // If alive, ICU patient recovers
                                }
                            }
                            if (timer_ptr[i] == 20) {
                                if (amrex::Random(engine) < CVF[age_group_ptr[i]]) {
                                    amrex::Gpu::Atomic::AddNoRet(
                                        &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                DiseaseStats::death), 1.0_rt);
                                    status_ptr[i] = Status::dead;
                                    //pstruct_ptr[i].id() = -pstruct_ptr[i].id();
                                }

                                amrex::Gpu::Atomic::AddNoRet(
                                                             &ds_arr(home_i_ptr[i], home_j_ptr[i], 0,
                                                                     DiseaseStats::ventilator), -1.0_rt);
                                if (status_ptr[i] != Status::dead) {
                                    status_ptr[i] = Status::immune;  // If alive, ventilated patient recovers
                                }
                            }
                        }
                        else { // not hospitalized, recover once not infectious
                            if (counter_ptr[i] >= (incubation_period_ptr[i] + infectious_period_ptr[i])) {
                                status_ptr[i] = Status::immune;
                            }
                        }
                    }
                }
            });
        }
    }
}

/*! \brief Interaction between agents

    Simulate the interactions between agents and compute the infection probability
    for each agent based on the number of infected agents at the same location:

    + Create bins of agents (see #amrex::GetParticleBin, #amrex::DenseBins) with
      their current locations:
      + The bin size is 1 cell
      + #amrex::GetParticleBin maps a particle to its bin index
      + amrex::DenseBins::build() creates the bin-sorted array of particle indices and
        the offset array for each bin (where the offset of a bin is its starting location
        in the bin-sorted array of particle indices).

    + For each bin:
      + Compute the total number of infected agents for each of the two strains.
      + For each agent in the bin, if they are not already infected or immune, infect them
        with a probability of 0.00001 and 0.00002 times the number of infections for each
        strain respectively.
*/
void AgentContainer::interactAgents ()
{
    BL_PROFILE("AgentContainer::interactAgents");

    IntVect bin_size = {AMREX_D_DECL(1, 1, 1)};
    for (int lev = 0; lev < numLevels(); ++lev)
    {
        const Geometry& geom = Geom(lev);
        const auto dxi = geom.InvCellSizeArray();
        const auto plo = geom.ProbLoArray();
        const auto domain = geom.Domain();

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            amrex::DenseBins<ParticleType> bins;
            auto& ptile = ParticlesAt(lev, mfi);
            auto& aos   = ptile.GetArrayOfStructs();
            const size_t np = aos.numParticles();
            auto pstruct_ptr = aos().dataPtr();

            const Box& box = mfi.validbox();

            int ntiles = numTilesInBox(box, true, bin_size);

            bins.build(np, pstruct_ptr, ntiles, GetParticleBin{plo, dxi, domain, bin_size, box});
            auto inds = bins.permutationPtr();
            auto offsets = bins.offsetsPtr();

            auto& soa   = ptile.GetStructOfArrays();
            auto status_ptr = soa.GetIntData(IntIdx::status).data();
            auto strain_ptr = soa.GetIntData(IntIdx::strain).data();
            auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();

            amrex::ParallelForRNG( bins.numBins(),
            [=] AMREX_GPU_DEVICE (int i_cell, amrex::RandomEngine const& engine) noexcept
            {
                auto cell_start = offsets[i_cell];
                auto cell_stop  = offsets[i_cell+1];

                // compute the number of infected in this cell
                int num_infected[2] = {0, 0};
                for (unsigned int i = cell_start; i < cell_stop; ++i) {
                    auto pindex = inds[i];
                    if (status_ptr[pindex] == 1) {
                        ++num_infected[strain_ptr[pindex]];
                    }
                }

                // second pass - infection prob is propto num_infected
                for (unsigned int i = cell_start; i < cell_stop; ++i) {
                    auto pindex = inds[i];
                    if ( status_ptr[pindex] != Status::infected &&
                         status_ptr[pindex] != Status::immune) {
                        if (amrex::Random(engine) < 0.0001*num_infected[0]) {
                            strain_ptr[pindex] = 0;
                            status_ptr[pindex] = Status::infected;
                            counter_ptr[pindex] = 0;
                        } else if (amrex::Random(engine) < 0.0002*num_infected[1]) {
                            strain_ptr[pindex] = 1;
                            status_ptr[pindex] = Status::infected;
                            counter_ptr[pindex] = 0;
                        }
                    }
                }
            });
            amrex::Gpu::synchronize();
        }
    }
}

/*! \brief Infect agents based on their current status and the computed probability of infection.
    The infection probability is computed in AgentContainer::interactAgentsHomeWork() or
    AgentContainer::interactAgents()
*/
void AgentContainer::infectAgents ()
{
    BL_PROFILE("AgentContainer::infectAgents");

    for (int lev = 0; lev <= finestLevel(); ++lev)
    {
        auto& plev  = GetParticles(lev);

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            int gid = mfi.index();
            int tid = mfi.LocalTileIndex();
            auto& ptile = plev[std::make_pair(gid, tid)];
            auto& soa   = ptile.GetStructOfArrays();
            const auto np = ptile.numParticles();
            auto status_ptr = soa.GetIntData(IntIdx::status).data();
            auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();
            auto prob_ptr = soa.GetRealData(RealIdx::prob).data();
            auto incubation_period_ptr = soa.GetRealData(RealIdx::incubation_period).data();
            auto infectious_period_ptr = soa.GetRealData(RealIdx::infectious_period).data();
            auto symptomdev_period_ptr = soa.GetRealData(RealIdx::symptomdev_period).data();

            auto* lparm = d_parm;

            amrex::ParallelForRNG( np,
            [=] AMREX_GPU_DEVICE (int i, amrex::RandomEngine const& engine) noexcept
            {
                prob_ptr[i] = 1.0 - prob_ptr[i];
                if ( status_ptr[i] == Status::never ||
                     status_ptr[i] == Status::susceptible ) {
                    if (amrex::Random(engine) < prob_ptr[i]) {
                        status_ptr[i] = Status::infected;
                        counter_ptr[i] = 0.0;
                        incubation_period_ptr[i] = amrex::RandomNormal(lparm->incubation_length_mean, lparm->incubation_length_std, engine);
                        infectious_period_ptr[i] = amrex::RandomNormal(lparm->infectious_length_mean, lparm->infectious_length_std, engine);
                        symptomdev_period_ptr[i] = amrex::RandomNormal(lparm->symptomdev_length_mean, lparm->symptomdev_length_std, engine);
                        return;
                    }
                }
            });
        }
    }
}

/*! \brief Interaction between agents at home and workplace

    Simulate the interactions between agents at home and workplace and compute
    the infection probability for each agent:

    + For home and workplace, create bins of agents if not already created (see
      #amrex::GetParticleBin, #amrex::DenseBins):
      + The bin size is 1 cell
      + #amrex::GetParticleBin maps a particle to its bin index
      + amrex::DenseBins::build() creates the bin-sorted array of particle indices and
        the offset array for each bin (where the offset of a bin is its starting location
        in the bin-sorted array of particle indices).

    + For each agent *i* in the bin-sorted array of agents:
      + Find its bin and the range of indices in the bin-sorted array for agents in its bin
      + If the agent is #Status::immune, do nothing.
      + If the agent is #Status::infected with the number of days infected (RealIdx::disease_counter)
        less than the #DiseaseParm::incubation_length, do nothing.
      + Else, for each agent *j* in the same bin:
        + If the agent is #Status::immune, do nothing.
        + If the agent is #Status::infected with the number of days infected (RealIdx::disease_counter)
          less than the #DiseaseParm::incubation_length, do nothing.
        + If *i* is not infected and *j* is infected, compute probability of *i* getting infected
          from *j* (see below).

    Summary of how the probability of agent A getting infected from agent B is computed:
    + Compute infection probability reduction factor from vaccine efficacy (#DiseaseParm::vac_eff)
    + Within family - if their IntIdx::nborhood and IntIdx::family indices are same,
      and the agents are at home:
      + If B is a child, use the appropriate transmission probability (#DiseaseParm::xmit_child_SC or
        #DiseaseParm::xmit_child) depending on whether B goes to school or not (#IntIdx::school)
      + If B is an adult, use the appropriate transmission probability (#DiseaseParm::xmit_adult_SC or
        #DiseaseParm::xmit_adult) depending on whether B works at a school or not (#IntIdx::school)
    + Within neighborhood - if their IntIdx::nborhood are same, the agents are not under
      quarrantine (#IntIdx::withdrawn), and the agents are not at work:
      + If B is a child, use the appropriate transmission probability (#DiseaseParm::xmit_nc_child_SC or
        #DiseaseParm::xmit_nc_child) depending on whether B goes to school or not (#IntIdx::school)
      + If B is an adult, use the appropriate transmission probability (#DiseaseParm::xmit_nc_adult_SC or
        #DiseaseParm::xmit_nc_adult) depending on whether B works at a school or not (#IntIdx::school)
    + At workplace - if agents are at work, and B has a workgroup and work location assigned: If A
      and B have the same workgroup and work location, use the workplace transmission
      probability (#DiseaseParm::xmit_work).
    + At school - if A and B are in the same school (#IntIdx::school) in the same neighborhood,
      and they are at school:
      + If both A and B are children: use #DiseaseParm::xmit_school
      + If B is a child, and A is an adult, use #DiseaseParm::xmit_sch_c2a
      + If A is a child, and B is an adult, use #DiseaseParm::xmit_sch_a2c
*/
void AgentContainer::interactAgentsHomeWork ( MultiFab& /*mask_behavior*/ /*!< Masking behavior */,
                                              bool home /*!< At home (true) or at work (false) */ )
{
    BL_PROFILE("AgentContainer::interactAgentsHomeWork");

    const bool DAYTIME = !home;
    IntVect bin_size = {AMREX_D_DECL(1, 1, 1)};
    for (int lev = 0; lev < numLevels(); ++lev)
    {
        const Geometry& geom = Geom(lev);
        const auto dxi = geom.InvCellSizeArray();
        const auto plo = geom.ProbLoArray();
        const auto domain = geom.Domain();

        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            auto pair_ind = std::make_pair(mfi.index(), mfi.LocalTileIndex());
            auto bins_ptr (home ? &m_bins_home[pair_ind] : &m_bins_work[pair_ind]);

            auto& ptile = ParticlesAt(lev, mfi);
            auto& aos   = ptile.GetArrayOfStructs();
            const auto np = aos.numParticles();
            auto pstruct_ptr = aos().dataPtr();

            const Box& box = mfi.validbox();
            int ntiles = numTilesInBox(box, true, bin_size);

            auto binner = GetParticleBin{plo, dxi, domain, bin_size, box};
            if (bins_ptr->numBins() < 0) {
                bins_ptr->build(BinPolicy::Serial, np, pstruct_ptr, ntiles, binner);
            }
            AMREX_ALWAYS_ASSERT(np == bins_ptr->numItems());
            amrex::Gpu::synchronize();
        }

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
        for(MFIter mfi = MakeMFIter(lev, TilingIfNotGPU()); mfi.isValid(); ++mfi)
        {
            auto pair_ind = std::make_pair(mfi.index(), mfi.LocalTileIndex());
            auto bins_ptr (home ? &m_bins_home[pair_ind] : &m_bins_work[pair_ind]);

            auto& ptile = ParticlesAt(lev, mfi);
            auto& aos   = ptile.GetArrayOfStructs();
            const auto np = aos.numParticles();
            auto pstruct_ptr = aos().dataPtr();

            auto binner = GetParticleBin{plo, dxi, domain, bin_size, mfi.validbox()};
            AMREX_ALWAYS_ASSERT(bins_ptr->numBins() >= 0);
            auto inds = bins_ptr->permutationPtr();
            auto offsets = bins_ptr->offsetsPtr();

            auto& soa   = ptile.GetStructOfArrays();
            auto status_ptr = soa.GetIntData(IntIdx::status).data();
            auto age_group_ptr = soa.GetIntData(IntIdx::age_group).data();

            //auto home_i_ptr = soa.GetIntData(IntIdx::home_i).data();
            //auto home_j_ptr = soa.GetIntData(IntIdx::home_j).data();
            auto work_i_ptr = soa.GetIntData(IntIdx::work_i).data();
            //auto work_j_ptr = soa.GetIntData(IntIdx::work_j).data();

            //auto mask_arr = mask_behavior[mfi].array();

            //auto strain_ptr = soa.GetIntData(IntIdx::strain).data();
            //auto timer_ptr = soa.GetRealData(RealIdx::timer).data();
            auto family_ptr = soa.GetIntData(IntIdx::family).data();
            auto nborhood_ptr = soa.GetIntData(IntIdx::nborhood).data();
            auto school_ptr = soa.GetIntData(IntIdx::school).data();
            auto withdrawn_ptr = soa.GetIntData(IntIdx::withdrawn).data();
            auto workgroup_ptr = soa.GetIntData(IntIdx::workgroup).data();
            auto prob_ptr = soa.GetRealData(RealIdx::prob).data();
            auto counter_ptr = soa.GetRealData(RealIdx::disease_counter).data();
            auto incubation_period_ptr = soa.GetRealData(RealIdx::incubation_period).data();
            //auto symptomdev_period_ptr = soa.GetRealData(RealIdx::symptomdev_period).data();

            auto* lparm = d_parm;
            amrex::ParallelFor( bins_ptr->numItems(), [=] AMREX_GPU_DEVICE (int ii) noexcept
            {
                auto i = inds[ii];
                int i_cell = binner(pstruct_ptr[i]);
                auto cell_start = offsets[i_cell];
                auto cell_stop  = offsets[i_cell+1];

                AMREX_ALWAYS_ASSERT( (Long) i < np);
                if (status_ptr[i] == Status::immune) { return; }
                if (status_ptr[i] == Status::dead) { return; }
                if (status_ptr[i] == Status::infected && counter_ptr[i] < incubation_period_ptr[i]) { return; }  // incubation stage
                //amrex::Real i_mask = mask_arr(home_i_ptr[i], home_j_ptr[i], 0);
                for (unsigned int jj = cell_start; jj < cell_stop; ++jj) {
                    auto j = inds[jj];
                    if (i == j) {continue;}
                    AMREX_ALWAYS_ASSERT( (Long) j < np);
                    //amrex::Real j_mask = mask_arr(home_i_ptr[j], home_j_ptr[j], 0);
                    if (status_ptr[j] == Status::immune) {continue;}
                    if (status_ptr[j] == Status::dead) {continue;}
                    if (status_ptr[j] == Status::infected && counter_ptr[j] < incubation_period_ptr[j]) { continue; }  // incubation stage

                    if (status_ptr[j] == Status::infected &&
                        (status_ptr[i] != Status::infected && status_ptr[i] != Status::dead)) {
                        if (counter_ptr[j] < incubation_period_ptr[j]) { continue; }
                        // j can infect i
                        amrex::Real infect = lparm->infect;
                        infect *= lparm->vac_eff;
                        //infect *= i_mask;
                        //infect *= j_mask;

                        amrex::Real social_scale = 1.0;  // TODO this should vary based on cell
                        amrex::Real work_scale = 1.0;  // TODO this should vary based on cell

                        amrex::ParticleReal prob = 1.0;
                        /* Determine what connections these individuals have */
                        if ((nborhood_ptr[i] == nborhood_ptr[j]) && (family_ptr[i] == family_ptr[j]) && (! DAYTIME)) {
                            if (age_group_ptr[j] <= 1) {  /* Transmitter j is a child */
                                if (school_ptr[j] < 0) { // not attending school, use _SC contacts
                                    prob *= 1.0 - infect * lparm->xmit_child_SC[age_group_ptr[i]];
                                } else {
                                    prob *= 1.0 - infect * lparm->xmit_child[age_group_ptr[i]];
                                }
                            } else {
                                if (school_ptr[j] < 0) {  // not attending school, use _SC contacts
                                    prob *= 1.0 - infect * lparm->xmit_adult_SC[age_group_ptr[i]];
                                } else {
                                    prob *= 1.0 - infect * lparm->xmit_adult[age_group_ptr[i]];
                                }
                            }
                        }

                        /* check for common neighborhood cluster: */
                        else if ((nborhood_ptr[i] == nborhood_ptr[j]) && (!withdrawn_ptr[i]) && (!withdrawn_ptr[j]) && ((family_ptr[i] / 4) == (family_ptr[j] / 4)) && (!DAYTIME)) {
                            if (age_group_ptr[j] <= 1) {  /* Transmitter i is a child */
                                if (school_ptr[j] < 0) { // not attending school, use _SC contacts
                                    prob *= 1.0 - infect * lparm->xmit_nc_child_SC[age_group_ptr[i]] * social_scale;
                                } else {
                                    prob *= 1.0 - infect * lparm->xmit_nc_child[age_group_ptr[i]] * social_scale;
                                }
                            } else {
                                if (school_ptr[j] < 0) {  // not attending school, use _SC contacts
                                    prob *= 1.0 - infect * lparm->xmit_nc_adult_SC[age_group_ptr[i]] * social_scale;
                                } else {
                                    prob *= 1.0 - infect * lparm->xmit_nc_adult[age_group_ptr[i]] * social_scale;
                                }
                            }
                        }

                        /* Home isolation or household quarantine? */  // TODO - be careful about withdrawn versus at home...
                        if ( (!withdrawn_ptr[i]) && (!withdrawn_ptr[j]) ) {

                            // school < 0 means a child normally attends school, but not today
                            /* Should always be in the same community = same cell */
                            if (school_ptr[j] < 0) {  // not attending school, use _SC contacts
                                prob *= 1.0 - infect * lparm->xmit_comm_SC[age_group_ptr[i]] * social_scale;
                            } else {
                                prob *= 1.0 - infect * lparm->xmit_comm[age_group_ptr[i]] * social_scale;
                            }

                            /* Workgroup transmission */
                            if (DAYTIME && workgroup_ptr[j] && (work_i_ptr[j] >= 0)) { // transmitter j at work
                                if ((work_i_ptr[i] >= 0) && (workgroup_ptr[i] == workgroup_ptr[j])) {  // coworker
                                    prob *= 1.0 - infect * lparm->xmit_work * work_scale;
                                }
                            }

                            /* Neighborhood? */
                            if (nborhood_ptr[i] == nborhood_ptr[j]) {
                                if (school_ptr[j] < 0) { // not attending school, use _SC contacts
                                    prob *= 1.0 - infect * lparm->xmit_hood_SC[age_group_ptr[i]] * social_scale;
                                } else {
                                    prob *= 1.0 - infect * lparm->xmit_hood[age_group_ptr[i]] * social_scale;
                                }

                                if ((school_ptr[i] == school_ptr[j]) && DAYTIME) {
                                    if (school_ptr[i] > 5) {
                                        /* Playgroup */
                                        prob *= 1.0 - infect * lparm->xmit_school[6] * social_scale;
                                    } else if (school_ptr[i] == 5) {
                                        /* Day care */
                                        prob *= 1.0 - infect * lparm->xmit_school[5] * social_scale;
                                    }
                                }
                            }  /* same neighborhood */

                            /* Elementary/middle/high school in common */
                            if ((school_ptr[i] == school_ptr[j]) && DAYTIME &&
                                (school_ptr[i] > 0) && (school_ptr[i] < 5)) {
                                if (age_group_ptr[i] <= 1) {  /* Receiver i is a child */
                                    if (age_group_ptr[j] <= 1) {  /* Transmitter j is a child */
                                        prob *= 1.0 - infect * lparm->xmit_school[school_ptr[i]] * social_scale;
                                    } else {   // Adult teacher/staff -> child student transmission
                                        prob *= 1.0 - infect * lparm->xmit_sch_a2c[school_ptr[i]] * social_scale;
                                    }
                                } else if (age_group_ptr[j] <= 1) {  // Child student -> adult teacher/staff
                                    prob *= 1.0 - infect * lparm->xmit_sch_c2a[school_ptr[i]] * social_scale;
                                }
                            }
                        }  /* within society */
                        Gpu::Atomic::Multiply(&prob_ptr[i], prob);
                    }
                }
            });
            amrex::Gpu::synchronize();
        }
    }
}

/*! \brief Computes the number of agents with various #Status in each grid cell of the
    computational domain.

    Given a MultiFab with at least 5 components that is defined with the same box array and
    distribution mapping as this #AgentContainer, the MultiFab will contain (at the end of
    this function) the following *in each cell*:
    + component 0: total number of agents in this grid cell.
    + component 1: number of agents that have never been infected (#Status::never)
    + component 2: number of agents that are infected (#Status::infected)
    + component 3: number of agents that are immune (#Status::immune)
    + component 4: number of agents that are susceptible infected (#Status::susceptible)
*/
void AgentContainer::generateCellData (MultiFab& mf /*!< MultiFab with at least 5 components */) const
{
    BL_PROFILE("AgentContainer::generateCellData");

    const int lev = 0;

    AMREX_ASSERT(OK());
    AMREX_ASSERT(numParticlesOutOfRange(*this, 0) == 0);

    const auto& geom = Geom(lev);
    const auto plo = geom.ProbLoArray();
    const auto dxi = geom.InvCellSizeArray();
    const auto domain = geom.Domain();
    amrex::ParticleToMesh(*this, mf, lev,
        [=] AMREX_GPU_DEVICE (const SuperParticleType& p,
                              amrex::Array4<amrex::Real> const& count)
        {
            int status = p.idata(0);
            auto iv = getParticleCell(p, plo, dxi, domain);
            amrex::Gpu::Atomic::AddNoRet(&count(iv, 0), 1.0_rt);
            if (status == Status::never) {
                amrex::Gpu::Atomic::AddNoRet(&count(iv, 1), 1.0_rt);
            }
            else if (status == Status::infected) {
                amrex::Gpu::Atomic::AddNoRet(&count(iv, 2), 1.0_rt);
            }
            else if (status == Status::immune) {
                amrex::Gpu::Atomic::AddNoRet(&count(iv, 3), 1.0_rt);
            }
            else if (status == Status::susceptible) {
                amrex::Gpu::Atomic::AddNoRet(&count(iv, 4), 1.0_rt);
            }
        }, false);
}

/*! \brief Computes the total number of agents with each #Status

    Returns a vector with 5 components corresponding to each value of #Status; each element is
    the total number of agents at a step with the corresponding #Status (in that order).
*/
std::array<Long, 5> AgentContainer::printTotals () {
    BL_PROFILE("printTotals");
    amrex::ReduceOps<ReduceOpSum, ReduceOpSum, ReduceOpSum, ReduceOpSum, ReduceOpSum> reduce_ops;
    auto r = amrex::ParticleReduce<ReduceData<int,int,int,int,int>> (
                  *this, [=] AMREX_GPU_DEVICE (const SuperParticleType& p) noexcept
                  -> amrex::GpuTuple<int,int,int,int,int>
              {
                  int s[5] = {0, 0, 0, 0, 0};
                  AMREX_ALWAYS_ASSERT(p.idata(IntIdx::status) >= 0);
                  AMREX_ALWAYS_ASSERT(p.idata(IntIdx::status) <= 4);
                  s[p.idata(IntIdx::status)] = 1;
                  return {s[0], s[1], s[2], s[3], s[4]};
              }, reduce_ops);

    std::array<Long, 5> counts = {amrex::get<0>(r), amrex::get<1>(r), amrex::get<2>(r), amrex::get<3>(r),
                                  amrex::get<4>(r)};
    ParallelDescriptor::ReduceLongSum(&counts[0], 5, ParallelDescriptor::IOProcessorNumber());
    // amrex::Print() << "Never infected: " << counts[0] << "\n";
    // amrex::Print() << "Infected: " << counts[1] << "\n";
    // amrex::Print() << "Immune: " << counts[2] << "\n";
    // amrex::Print() << "Previously infected: " << counts[3] << "\n";
    // amrex::Print() << "Deaths: " << counts[4] << "\n";
    return counts;
}
