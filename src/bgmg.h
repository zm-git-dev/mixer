/*
  bgmg - tool to calculate log likelihood of BGMG and UGMG mixture models
  Copyright (C) 2018 Oleksandr Frei 

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include "bgmg_export.h"

#define VERSION "v1.2.0"

extern "C" {
  // "System" functions - error diagnostics, logging debug info to a file, memory management, status.
  DLL_PUBLIC const char* bgmg_get_last_error();
  DLL_PUBLIC void bgmg_init_log(const char* file);
  DLL_PUBLIC void bgmg_log_message(const char* message);
  DLL_PUBLIC int64_t bgmg_dispose(int context_id);
  DLL_PUBLIC const char* bgmg_status(int context_id);

  // Parse input files
  // - bim - plink .bim files (can have chromosome label @)
  // - frq - plink .frq files (can have chromosome label @)
  // - chr_labels - list of chromosome labels (can be empty, default to 1:22)
  // - trait1_file - summary stats (must have SNP, A1, A2, Z, N)
  // - trait2_file
  // - exclude, extract - list of SNPs (without header) to use for the analysis (see plink --exclude and --extract options)
  // Automatically calls the following:
  // - set_tag_indices
  // - set_chrnumvec
  // - set_zvec, set_nvec (if trait1_file and/or trait2_file is available)
  // - set_mafvec (if frq file is specified)
  DLL_PUBLIC int64_t bgmg_init(int context_id, const char* bim_file, const char* frq_file, const char* chr_labels, const char* trait1_file, const char* trait2_file, const char* exclude, const char* extract);
  DLL_PUBLIC int64_t bgmg_convert_plink_ld(int context_id, const char* plink_ld_gz, const char* plink_ld_bin);

  // API to work with "defvec". Here 
  // - num_snp is how many SNPs there is in the reference (particularly, in LD files and mafvec)
  // - num_tag is how many SNPs there is in the GWAS (zvec, nvec, weights)
  // Tag snps must be a subset of the reference. tag_indices give indices of tag SNPs in the reference.
  DLL_PUBLIC int64_t bgmg_set_tag_indices(int context_id, int num_snp, int num_tag, int* tag_indices);
  DLL_PUBLIC int64_t bgmg_get_num_tag(int context_id);
  DLL_PUBLIC int64_t bgmg_get_num_snp(int context_id);
  DLL_PUBLIC int64_t bgmg_retrieve_tag_indices(int context_id, int num_tag, int* tag_indices);

  DLL_PUBLIC int64_t bgmg_set_chrnumvec(int context_id, int length, int* values);
  DLL_PUBLIC int64_t bgmg_retrieve_chrnumvec(int context_id, int length, int* buffer);

  // Set variouns options:
  // diag, kmax, r2min, num_components, seed, fast_cost, threads; refer to BgmgCalculator::set_option for a full list.
  // NB. Most options reset LD structure, you'll have to bgmg_set_ld_r2_coo / bgmg_set_ld_r2_csr again.
  DLL_PUBLIC int64_t bgmg_set_option(int context_id, char* option, double value);

  // API to populate and retrieve zvec, nvec, mafvec
  DLL_PUBLIC int64_t bgmg_set_zvec(int context_id, int trait, int length, float* values);
  DLL_PUBLIC int64_t bgmg_set_nvec(int context_id, int trait, int length, float* values);
  DLL_PUBLIC int64_t bgmg_set_causalbetavec(int context_id, int trait, int length, float* values);
  DLL_PUBLIC int64_t bgmg_set_mafvec(int context_id, int length, float* values);

  DLL_PUBLIC int64_t bgmg_retrieve_zvec(int context_id, int trait, int length, float* buffer);
  DLL_PUBLIC int64_t bgmg_retrieve_nvec(int context_id, int trait, int length, float* buffer);
  DLL_PUBLIC int64_t bgmg_retrieve_causalbetavec(int context_id, int trait, int length, float* buffer);
  DLL_PUBLIC int64_t bgmg_retrieve_mafvec(int context_id, int length, float* buffer);

  DLL_PUBLIC int64_t bgmg_retrieve_fixed_effect_delta(int context_id, int trait, int length, float* buffer);

  // API to populate LD structure
  // "from_file" expect a binary file produced by bgmg_calc_ld_matrix.
  // NB. we expect that this data originates from plink, where LD matrix is lower triangular, diagonal not included. So snpA must be always lower than snpB.
  DLL_PUBLIC int64_t bgmg_set_ld_r2_coo(int context_id, int chr_label, int64_t length, int* snp_index, int* snp_other_index, float* r);
  DLL_PUBLIC int64_t bgmg_set_ld_r2_coo_from_file(int context_id, int chr_label, const char* filename);
  DLL_PUBLIC int64_t bgmg_set_ld_r2_csr(int context_id, int chr_label);

  // query LD structure of a given SNP or for a given chromosome
  DLL_PUBLIC int64_t bgmg_num_ld_r2_snp(int context_id, int snp_index);
  DLL_PUBLIC int64_t bgmg_retrieve_ld_r2_snp(int context_id, int snp_index, int length, int* tag_index, float* r2);
  DLL_PUBLIC int64_t bgmg_num_ld_r2_chr(int context_id, int chr_label);
  DLL_PUBLIC int64_t bgmg_retrieve_ld_r2_chr(int context_id, int chr_label, int64_t length, int* snp_index, int* tag_index, float* r2);
  DLL_PUBLIC int64_t bgmg_num_ld_r2_snp_range(int context_id, int snp_index_from, int snp_index_to);
  DLL_PUBLIC int64_t bgmg_retrieve_ld_r2_snp_range(int context_id, int snp_index_from, int snp_index_to, int64_t length, int* snp_index, int* tag_index, float* r2);
  
  // Set weights, either explicitly or based on random pruning
  DLL_PUBLIC int64_t bgmg_set_weights(int context_id, int length, float* values);
  DLL_PUBLIC int64_t bgmg_set_weights_randprune(int context_id, int n, float r2, const char* exclude, const char* extract);
  DLL_PUBLIC int64_t bgmg_retrieve_weights(int context_id, int length, float* buffer);
  
  // Perform informed clumping - i.e. pick the largest value in the buffer, remove everything in LD with it, and continue until no work is left
  DLL_PUBLIC int64_t bgmg_perform_ld_clump(int context_id, float r2, int length, float* buffer);
  
  // Retrieve certain aspects of the LD structure. Mainly for debugging purpose.
  DLL_PUBLIC int64_t bgmg_retrieve_tag_r2_sum(int context_id, int component_id, float num_causal, int length, float* buffer);
  DLL_PUBLIC int64_t bgmg_retrieve_ld_sum_r2(int context_id, int length, float* buffer);
  DLL_PUBLIC int64_t bgmg_retrieve_ld_sum_r4(int context_id, int length, float* buffer);
  
  // Calc univariate cost function and pdf
  DLL_PUBLIC double bgmg_calc_unified_univariate_cost(int context_id, int trait_index, int num_components, int num_snp, float* pi_vec, float* sig2_vec, float sig2_zeroA, float sig2_zeroC, float sig2_zeroL, float* aux);
  DLL_PUBLIC int64_t bgmg_calc_unified_univariate_pdf(int context_id, int trait_index, int num_components, int num_snp, float* pi_vec, float* sig2_vec, float sig2_zeroA, float sig2_zeroC, float sig2_zeroL, int length, float* zvec, float* pdf);
  DLL_PUBLIC int64_t bgmg_calc_unified_univariate_power(int context_id, int trait_index, int num_components, int num_snp, float* pi_vec, float* sig2_vec, float sig2_zeroA, float sig2_zeroC, float sig2_zeroL, float zthresh, int length, float* nvec, float* svec);
  DLL_PUBLIC int64_t bgmg_calc_unified_univariate_delta_posterior(int context_id, int trait_index, int num_components, int num_snp, float* pi_vec, float* sig2_vec, float sig2_zeroA, float sig2_zeroC, float sig2_zeroL, int length, float* c0, float* c1, float* c2);

  // Calc bivariate cost function and pdf
  DLL_PUBLIC double bgmg_calc_unified_bivariate_cost(int context_id, int num_snp, float* pi_vec, float* sig2_vec, float* rho_vec, float* sig2_zeroA, float* sig2_zeroC, float* sig2_zeroL, float rho_zeroA, float rho_zeroL, float* aux);
  DLL_PUBLIC int64_t bgmg_calc_unified_bivariate_pdf(int context_id, int num_snp, float* pi_vec, float* sig2_vec, float* rho_vec, float* sig2_zeroA, float* sig2_zeroC, float* sig2_zeroL, float rho_zeroA, float rho_zeroL, int length, float* zvec1, float* zvec2, float* pdf);
  DLL_PUBLIC int64_t bgmg_calc_unified_bivariate_delta_posterior(int context_id, int num_snp, float* pi_vec, float* sig2_vec, float* rho_vec, float* sig2_zeroA, float* sig2_zeroC, float* sig2_zeroL, float rho_zeroA, float rho_zeroL,
                                                                 int length, float* c00, float* c10, float* c01, float* c20, float* c11, float* c02);

  // estimate LD structure
  DLL_PUBLIC int64_t bgmg_calc_ld_matrix(const char* bfile, const char* outfile, double r2min, double ldscore_r2min, int ld_window, float ld_window_kb);
}

