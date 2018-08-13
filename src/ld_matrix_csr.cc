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

#include "ld_matrix_csr.h"

#include <assert.h>

void find_hvec(TagToSnpMapping& mapping, std::vector<float>* hvec) {
  const std::vector<float>& mafvec = mapping.mafvec();
  hvec->resize(mafvec.size(), 0.0f);
  for (int snp_index = 0; snp_index < mafvec.size(); snp_index++) {
    hvec->at(snp_index) = 2.0f * mafvec[snp_index] * (1.0f - mafvec[snp_index]);
  }
}

int64_t LdMatrixCsr::set_ld_r2_coo(const std::string& filename, float r2_min) {
  std::ifstream is(filename, std::ifstream::binary);
  if (!is) BGMG_THROW_EXCEPTION(::std::runtime_error("can't open" + filename));
  if (sizeof(int) != 4) BGMG_THROW_EXCEPTION("sizeof(int) != 4, internal error in BGMG cpp"); // int -> int32_t

  int64_t numel;
  is.read(reinterpret_cast<char*>(&numel), sizeof(int64_t));
  LOG << " set_ld_r2_coo(filename=" << filename << "), reading " << numel << " elements...";

  std::vector<int> snp_index(numel, 0), tag_index(numel, 0);
  std::vector<float> r2(numel, 0.0f);

  is.read(reinterpret_cast<char*>(&snp_index[0]), numel * sizeof(int));
  is.read(reinterpret_cast<char*>(&tag_index[0]), numel * sizeof(int));
  is.read(reinterpret_cast<char*>(&r2[0]), numel * sizeof(float));

  if (!is) BGMG_THROW_EXCEPTION(::std::runtime_error("can't read from " + filename));
  is.close();

  return set_ld_r2_coo(numel, &snp_index[0], &tag_index[0], &r2[0], r2_min);
}

int64_t LdMatrixCsr::set_ld_r2_coo(int64_t length, int* snp_index, int* tag_index, float* r2, float r2_min) {
  if (!combined_.csr_ld_r2_.empty()) BGMG_THROW_EXCEPTION(::std::runtime_error("can't call set_ld_r2_coo after set_ld_r2_csr"));
  if (mapping_.mafvec().empty()) BGMG_THROW_EXCEPTION(::std::runtime_error("can't call set_ld_r2_coo before set_mafvec"));
  LOG << ">set_ld_r2_coo(length=" << length << "); ";

  if (ld_tag_sum_adjust_for_hvec_ == nullptr) {
    ld_tag_sum_adjust_for_hvec_ = std::make_shared<LdTagSum>(LD_TAG_COMPONENT_COUNT, mapping_.num_tag());
    ld_tag_sum_ = std::make_shared<LdTagSum>(LD_TAG_COMPONENT_COUNT, mapping_.num_tag());
  }

  for (int64_t i = 0; i < length; i++)
    if (snp_index[i] == tag_index[i])
      BGMG_THROW_EXCEPTION(::std::runtime_error("snp_index[i] == tag_index[i] --- unexpected for ld files created via plink"));

  for (int64_t i = 0; i < length; i++) {
    if (!std::isfinite(r2[i])) BGMG_THROW_EXCEPTION(::std::runtime_error("encounter undefined values"));
  }

  SimpleTimer timer(-1);

  std::vector<float> hvec;
  find_hvec(mapping_, &hvec);

  int was = combined_.coo_ld_.size();
  for (int64_t i = 0; i < length; i++) {
    CHECK_SNP_INDEX(mapping_, snp_index[i]); CHECK_SNP_INDEX(mapping_, tag_index[i]);

    int ld_component = (r2[i] < r2_min) ? LD_TAG_COMPONENT_BELOW_R2MIN : LD_TAG_COMPONENT_ABOVE_R2MIN;
    if (mapping_.is_tag()[tag_index[i]]) ld_tag_sum_adjust_for_hvec_->store(ld_component, mapping_.snp_to_tag()[tag_index[i]], r2[i] * hvec[snp_index[i]]);
    if (mapping_.is_tag()[snp_index[i]]) ld_tag_sum_adjust_for_hvec_->store(ld_component, mapping_.snp_to_tag()[snp_index[i]], r2[i] * hvec[tag_index[i]]);

    if (mapping_.is_tag()[tag_index[i]]) ld_tag_sum_->store(ld_component, mapping_.snp_to_tag()[tag_index[i]], r2[i]);
    if (mapping_.is_tag()[snp_index[i]]) ld_tag_sum_->store(ld_component, mapping_.snp_to_tag()[snp_index[i]], r2[i]);

    if (r2[i] < r2_min) continue;
    // tricky part here is that we take into account snp_can_be_causal_
    // there is no reason to keep LD information about certain causal SNP if we never selecting it as causal
    // (see how snp_can_be_causal_ is created during find_snp_order() call)
    if (mapping_.snp_can_be_causal()[snp_index[i]] && mapping_.is_tag()[tag_index[i]]) combined_.coo_ld_.push_back(std::make_tuple(snp_index[i], mapping_.snp_to_tag()[tag_index[i]], r2[i]));
    if (mapping_.snp_can_be_causal()[tag_index[i]] && mapping_.is_tag()[snp_index[i]]) combined_.coo_ld_.push_back(std::make_tuple(tag_index[i], mapping_.snp_to_tag()[snp_index[i]], r2[i]));
  }
  LOG << "<set_ld_r2_coo: done; coo_ld_.size()=" << combined_.coo_ld_.size() << " (new: " << combined_.coo_ld_.size() - was << "), elapsed time " << timer.elapsed_ms() << " ms";
  return 0;
}

int64_t LdMatrixCsr::set_ld_r2_csr(float r2_min) {
  if (combined_.coo_ld_.empty())
    BGMG_THROW_EXCEPTION(::std::runtime_error("coo_ld_ is empty"));

  LOG << ">set_ld_r2_csr (coo_ld_.size()==" << combined_.coo_ld_.size() << "); ";

  SimpleTimer timer(-1);

  std::vector<float> hvec;
  find_hvec(mapping_, &hvec);

  LOG << " set_ld_r2_csr adds " << mapping_.tag_to_snp().size() << " elements with r2=1.0 to the diagonal of LD r2 matrix";
  for (int i = 0; i < mapping_.tag_to_snp().size(); i++) {
    combined_.coo_ld_.push_back(std::make_tuple(mapping_.tag_to_snp()[i], i, 1.0f));
    ld_tag_sum_adjust_for_hvec_->store(LD_TAG_COMPONENT_ABOVE_R2MIN, i, 1.0f * hvec[mapping_.tag_to_snp()[i]]);
    ld_tag_sum_->store(LD_TAG_COMPONENT_ABOVE_R2MIN, i, 1.0f);
  }

  LOG << " sorting ld r2 elements... ";
  SimpleTimer timer2(-1);
  // Use parallel sort? https://software.intel.com/en-us/articles/a-parallel-stable-sort-using-c11-for-tbb-cilk-plus-and-openmp
#if _OPENMP >= 200805
  pss::parallel_stable_sort(coo_ld_.begin(), coo_ld_.end(), std::less<std::tuple<int, int, float>>());
  LOG << " pss::parallel_stable_sort took " << timer.elapsed_ms() << "ms.";
#else
  std::sort(combined_.coo_ld_.begin(), combined_.coo_ld_.end());
  LOG << " std::sort took " << timer.elapsed_ms() << "ms.";
  LOG << " (to enable parallel sort build bgmglib with compiler that supports OpenMP 3.0";
#endif

  combined_.csr_ld_tag_index_.reserve(combined_.coo_ld_.size());
  combined_.csr_ld_r2_.reserve(combined_.coo_ld_.size());

  for (int64_t i = 0; i < combined_.coo_ld_.size(); i++) {
    combined_.csr_ld_tag_index_.push_back(std::get<1>(combined_.coo_ld_[i]));
    combined_.csr_ld_r2_.push_back(std::get<2>(combined_.coo_ld_[i]));
  }

  // find starting position for each snp
  combined_.csr_ld_snp_index_.resize(mapping_.snp_to_tag().size() + 1, combined_.coo_ld_.size());
  for (int64_t i = (combined_.coo_ld_.size() - 1); i >= 0; i--) {
    int snp_index = std::get<0>(combined_.coo_ld_[i]);
    combined_.csr_ld_snp_index_[snp_index] = i;
  }

  for (int i = (combined_.csr_ld_snp_index_.size() - 2); i >= 0; i--)
    if (combined_.csr_ld_snp_index_[i] > combined_.csr_ld_snp_index_[i + 1])
      combined_.csr_ld_snp_index_[i] = combined_.csr_ld_snp_index_[i + 1];

  LOG << "<set_ld_r2_csr (coo_ld_.size()==" << combined_.coo_ld_.size() << "); elapsed time " << timer.elapsed_ms() << " ms";
  combined_.coo_ld_.clear();
  validate_ld_r2_csr(r2_min);

  return 0;
}

int64_t LdMatrixCsr::validate_ld_r2_csr(float r2_min) {
  LOG << ">validate_ld_r2_csr(); ";
  SimpleTimer timer(-1);
  
  // Test correctness of sparse representation
  if (combined_.csr_ld_snp_index_.size() != (mapping_.num_snp() + 1)) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_snp_index_.size() != (num_snp_ + 1))"));
  for (int i = 0; i < combined_.csr_ld_snp_index_.size(); i++) if (combined_.csr_ld_snp_index_[i] < 0 || combined_.csr_ld_snp_index_[i] > combined_.csr_ld_r2_.size()) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_snp_index_[i] < 0 || csr_ld_snp_index_[i] > csr_ld_r2_.size()"));
  for (int i = 1; i < combined_.csr_ld_snp_index_.size(); i++) if (combined_.csr_ld_snp_index_[i - 1] > combined_.csr_ld_snp_index_[i]) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_snp_index_[i-1] > csr_ld_snp_index_[i]"));
  if (combined_.csr_ld_snp_index_.back() != combined_.csr_ld_r2_.size()) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_snp_index_.back() != csr_ld_r2_.size()"));
  if (combined_.csr_ld_tag_index_.size() != combined_.csr_ld_r2_.size()) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_tag_index_.size() != csr_ld_r2_.size()"));
  for (int64_t i = 0; i < combined_.csr_ld_tag_index_.size(); i++) if (combined_.csr_ld_tag_index_[i] < 0 || combined_.csr_ld_tag_index_[i] >= mapping_.num_tag()) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_tag_index_ < 0 || csr_ld_tag_index_ >= num_tag_"));

  // Test that all values are between zero and r2min
  for (int64_t i = 0; i < combined_.csr_ld_r2_.size(); i++) if (combined_.csr_ld_r2_[i] < r2_min || combined_.csr_ld_r2_[i] > 1.0f) BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_tag_index_ < 0 || csr_ld_tag_index_ >= num_tag_"));
  for (int64_t i = 0; i < combined_.csr_ld_r2_.size(); i++) if (!std::isfinite(combined_.csr_ld_r2_[i])) BGMG_THROW_EXCEPTION(std::runtime_error("!std::isfinite(csr_ld_r2_[i])"));

  // Test that LDr2 does not have duplicates
  for (int causal_index = 0; causal_index < mapping_.num_snp(); causal_index++) {
    const int64_t r2_index_from = combined_.csr_ld_snp_index_[causal_index];
    const int64_t r2_index_to = combined_.csr_ld_snp_index_[causal_index + 1];
    for (int64_t r2_index = r2_index_from; r2_index < (r2_index_to - 1); r2_index++) {
      if (combined_.csr_ld_tag_index_[r2_index] == combined_.csr_ld_tag_index_[r2_index + 1])
        BGMG_THROW_EXCEPTION(std::runtime_error("csr_ld_tag_index_[r2_index] == csr_ld_tag_index_[r2_index + 1]"));
    }
  }

  // Test that LDr2 is symmetric (as long as both SNPs are tag)
  // Test that LDr2 contains the diagonal
  for (int causal_index = 0; causal_index < mapping_.num_snp(); causal_index++) {
    if (!mapping_.is_tag()[causal_index]) continue;
    const int tag_index_of_the_snp = mapping_.snp_to_tag()[causal_index];

    const int64_t r2_index_from = combined_.csr_ld_snp_index_[causal_index];
    const int64_t r2_index_to = combined_.csr_ld_snp_index_[causal_index + 1];
    bool ld_r2_contains_diagonal = false;
    for (int64_t r2_index = r2_index_from; r2_index < r2_index_to; r2_index++) {
      const int tag_index = combined_.csr_ld_tag_index_[r2_index];
      const float r2 = combined_.csr_ld_r2_[r2_index];  // here we are interested in r2 (hvec is irrelevant)

      if (tag_index == tag_index_of_the_snp) ld_r2_contains_diagonal = true;
      float r2symm = find_and_retrieve_ld_r2(mapping_.tag_to_snp()[tag_index], tag_index_of_the_snp);
      if (!std::isfinite(r2symm)) BGMG_THROW_EXCEPTION(std::runtime_error("!std::isfinite(r2symm)"));
      if (r2symm != r2) BGMG_THROW_EXCEPTION(std::runtime_error("r2symm != r2"));
    }

    if (!ld_r2_contains_diagonal) BGMG_THROW_EXCEPTION(std::runtime_error("!ld_r2_contains_diagonal"));
  }

  LOG << "<validate_ld_r2_csr (); elapsed time " << timer.elapsed_ms() << " ms";
  return 0;
}

float LdMatrixCsr::find_and_retrieve_ld_r2(int snp_index, int tag_index) {
  auto r2_iter_from = combined_.csr_ld_tag_index_.begin() + combined_.csr_ld_snp_index_[snp_index];
  auto r2_iter_to = combined_.csr_ld_tag_index_.begin() + combined_.csr_ld_snp_index_[snp_index + 1];
  auto iter = std::lower_bound(r2_iter_from, r2_iter_to, tag_index);
  return (iter != r2_iter_to) ? combined_.csr_ld_r2_[iter - combined_.csr_ld_tag_index_.begin()] : NAN;
}

size_t LdMatrixCsr::log_diagnostics() {
  size_t mem_bytes_total = 0;
  for (int i = 0; i < chunks_.size(); i++) {
    LOG << " diag: LdMatrixCsr chunk " << i;
    mem_bytes_total += chunks_[i].log_diagnostics();
  }

  LOG << " diag: LdMatrixCsr combined ";
  mem_bytes_total += combined_.log_diagnostics();

  return mem_bytes_total;
}

size_t LdMatrixCsrChunk::log_diagnostics() {
  size_t mem_bytes = 0, mem_bytes_total = 0;
  LOG << " diag: csr_ld_snp_index_.size()=" << csr_ld_snp_index_.size();
  mem_bytes = csr_ld_tag_index_.size() * sizeof(int); mem_bytes_total += mem_bytes;
  LOG << " diag: csr_ld_tag_index_.size()=" << csr_ld_tag_index_.size() << " (mem usage = " << mem_bytes << " bytes)";
  mem_bytes = csr_ld_r2_.size() * sizeof(float); mem_bytes_total += mem_bytes;
  LOG << " diag: csr_ld_r2_.size()=" << csr_ld_r2_.size() << " (mem usage = " << mem_bytes << " bytes)";
  mem_bytes = coo_ld_.size() * (sizeof(float) + sizeof(int) + sizeof(int)); mem_bytes_total += mem_bytes;
  LOG << " diag: coo_ld_.size()=" << coo_ld_.size() << " (mem usage = " << mem_bytes << " bytes)";
  return mem_bytes_total;
}

void LdMatrixCsr::clear() {
  chunks_.clear();
  combined_.clear();
  if (ld_tag_sum_adjust_for_hvec_ != nullptr) ld_tag_sum_adjust_for_hvec_->clear();
  if (ld_tag_sum_ != nullptr) ld_tag_sum_->clear();
}

void LdMatrixCsrChunk::clear() {
  // clear all info about LD structure
  csr_ld_snp_index_.clear();
  csr_ld_tag_index_.clear();
  csr_ld_r2_.clear();
  coo_ld_.clear();
}