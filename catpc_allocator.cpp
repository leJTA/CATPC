#include "catpc_allocator.hpp"
#include "catpc_monitor.hpp"
#include "catpc_utils.hpp"

#include <vector>
#include <functional>
#include <bitset>
#include "pqos.h"

#include <boost/math/statistics/linear_regression.hpp>

#define MAX_NUM_WAYS 11

std::vector<double> way_occupancy_ratios;
std::vector<std::vector<std::reference_wrapper<double>>> CLOS_occupency_ratios;

std::vector<llc_ca> get_allocation_config() 
{
	int ret;
	unsigned l3cat_id_count, *l3cat_ids = NULL;
	std::vector<llc_ca> llc_ca_list;
	catpc_system_info si = get_system_info();
	
	/* Get CPU l3cat id information to set COS */
	l3cat_ids = pqos_cpu_get_l3cat_ids(si.p_cpu, &l3cat_id_count);
	if (l3cat_ids == NULL) {
		return {};
	}

	for (unsigned i = 0; i < l3cat_id_count; i++) {
		struct pqos_l3ca tab[PQOS_MAX_L3CA_COS];
		unsigned num = 0;
		ret = pqos_l3ca_get(l3cat_ids[i], PQOS_MAX_L3CA_COS, &num, tab);
		
		llc_ca_list.push_back(llc_ca());
		llc_ca_list.back().id = l3cat_ids[i];
		llc_ca_list.back().num_ways = si.l3_cap->u.l3ca->num_ways;
		llc_ca_list.back().way_size = si.l3_cap->u.l3ca->way_size;
		llc_ca_list.back().clos_count = num;

		if (ret == PQOS_RETVAL_OK) {
			for (unsigned n = 0; n < num; n++) {
				llc_ca_list.back().clos_list.push_back(CLOS());
				llc_ca_list.back().clos_list.back().id = tab[n].class_id;
				llc_ca_list.back().clos_list.back().mask = tab[n].u.ways_mask;
			}
		}
		else {
			return {};
		}
	}

	// Init occupancy ratios
	for (unsigned i = 0; i < si.l3_cap->u.l3ca->num_ways; ++i) {
		way_occupancy_ratios.push_back(0);
	}

	for (unsigned i = 0; i < llc_ca_list.back().clos_count; ++i) {
		CLOS_occupency_ratios.push_back(std::vector<std::reference_wrapper<double>>());
		std::bitset<MAX_NUM_WAYS> bitmask{ llc_ca_list.back().clos_list[i].mask };
		for (int j = 0; j < MAX_NUM_WAYS; ++j) {
			if (bitmask[j] == 1) {
				CLOS_occupency_ratios.back().push_back(way_occupancy_ratios[j]);
			}
		}
	}

	return llc_ca_list;
}

int perform_allocation(catpc_application* application_ptr)
{
	int ret = 0, sz = 0;
	pid_t pids[256];
	
	sz = get_pids_by_cmdline(pids, application_ptr->cmdline.c_str());
	for (int i = 0; i < sz; ++i) {
		ret = pqos_alloc_assoc_set_pid(pids[i], application_ptr->CLOS_id);
		if (ret != PQOS_RETVAL_OK) {
			return -1 * ret;
		}
	}
	return 0;
}

uint64_t get_required_llc(const std::map<uint64_t, double>& mrc, const std::vector<llc_ca>& llcs)
{	
	std::vector<double> x{};
	std::vector<double> y{};

	unsigned llc_size = llcs.size() * llcs[0].num_ways * llcs[0].way_size;	// size of last level cache

	for (const auto& [k, v] : mrc) {
		x.push_back(k);
		y.push_back(v);
	}

	while (x.size() > 1) {
		auto [c, slope] = boost::math::statistics::simple_ordinary_least_squares(x, y);
		if (slope * llc_size > -0.05) {
			return x[0];
		}

		x.erase(x.begin());
		y.erase(y.begin());
	}

	return x[0];
}

int perform_smart_allocation(catpc_application* application_ptr, std::vector<llc_ca>& llcs)
{
	int num_llcs = llcs.size();
	unsigned selected_CLOS_id = 0;
	unsigned way_size = llcs[0].way_size;
	double target_occupancy_ratio = -1, curr_occupancy_ratio = 0;
	
	// Search for the optimal CLOS
	for (unsigned i = 0; i < llcs[0].clos_count - 1; ++i) {
		double CLOS_occupancy = std::accumulate(CLOS_occupency_ratios[i].begin(), CLOS_occupency_ratios[i].end(), 0.0);
		curr_occupancy_ratio = (double)( application_ptr->required_llc + (CLOS_occupancy * num_llcs * way_size) ) / (CLOS_occupency_ratios[i].size() * num_llcs * way_size);
		
		if (
			(target_occupancy_ratio < 0) || 
			((curr_occupancy_ratio < target_occupancy_ratio) && (target_occupancy_ratio >= 1)) ||
			((curr_occupancy_ratio < 1) && (target_occupancy_ratio < 1) && (curr_occupancy_ratio > target_occupancy_ratio))
		){
			selected_CLOS_id = i;
			target_occupancy_ratio = curr_occupancy_ratio;
		}
	}
	application_ptr->CLOS_id = selected_CLOS_id;

	// Update CLOS occupancy
	for (unsigned i = 0; i < CLOS_occupency_ratios[selected_CLOS_id].size(); ++i) {
		CLOS_occupency_ratios[selected_CLOS_id][i] += (double) application_ptr->required_llc / (num_llcs * way_size * CLOS_occupency_ratios[selected_CLOS_id].size());
	}

	// Perform allocation
	int ret = 0, sz = 0;
	pid_t pids[256];
	sz = get_pids_by_cmdline(pids, application_ptr->cmdline.c_str());
	for (int i = 0; i < sz; ++i) {
		ret = pqos_alloc_assoc_set_pid(pids[i], selected_CLOS_id);
		if (ret != PQOS_RETVAL_OK) {
			return -1 * ret;
		}
	}

	pqos_l3ca tab[PQOS_MAX_L3CA_COS];
	unsigned num;
	tab[0].class_id = 0;
	for (unsigned i = 0; i < llcs.size(); ++i) {
		ret = pqos_l3ca_get(i, PQOS_MAX_L3CA_COS, &num, tab);	// num = clos_count

		llcs[i].clos_list[num - 1].mask = (0x7ff ^ llcs[i].clos_list[selected_CLOS_id].mask) | 0x400;
		tab[num - 1].u.ways_mask = llcs[i].clos_list[num - 1].mask;
		
		ret = pqos_l3ca_set(i, num, tab);
		if (ret != PQOS_RETVAL_OK) {
			return -1 * ret;
		}
	}
	
	return 0;
}

int remove_application(std::unordered_map<std::string, catpc_application*>& applications, const std::vector<llc_ca>& llcs, const std::string& cmdline)
{
	if (applications.find(cmdline) == applications.end()) {
		return -1;
	}
	
	catpc_application* app_ptr = applications[cmdline];
	int num_llcs = llcs.size();
	unsigned way_size = llcs[0].way_size;

	// Update CLOS occupancy
	for (unsigned i = 0; i < CLOS_occupency_ratios[app_ptr->CLOS_id].size(); ++i) {
		CLOS_occupency_ratios[app_ptr->CLOS_id][i] -= (double) app_ptr->required_llc / (num_llcs * way_size * CLOS_occupency_ratios[app_ptr->CLOS_id].size());
	}

	// Remove from app list
	applications.erase(cmdline);

	return 0;
}