/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "modules/planning/math/piecewise_jerk/piecewise_jerk_speed_problem.h"

#include <algorithm>

#include "cyber/common/log.h"

#include "modules/planning/common/planning_gflags.h"

namespace apollo {
namespace planning {

PiecewiseJerkSpeedProblem::PiecewiseJerkSpeedProblem(
    const size_t num_of_knots, const double delta_s,
    const std::array<double, 3>& x_init)
    : PiecewiseJerkProblem(num_of_knots, delta_s, x_init) {
  penalty_dx_.resize(num_of_knots_, 0.0);
}

void PiecewiseJerkSpeedProblem::set_x_ref(const double weight_x_ref,
                                          std::vector<double> x_ref) {
  CHECK_EQ(x_ref.size(), num_of_knots_);
  x_ref_ = std::move(x_ref);
  weight_x_ref_ = weight_x_ref;
  has_x_ref_ = true;
}

void PiecewiseJerkSpeedProblem::set_dx_ref(const double weight_dx_ref,
                                           const double dx_ref) {
  weight_dx_ref_ = weight_dx_ref;
  dx_ref_ = dx_ref;
  has_dx_ref_ = true;
}

void PiecewiseJerkSpeedProblem::set_penalty_dx(std::vector<double> penalty_dx) {
  CHECK_EQ(penalty_dx.size(), num_of_knots_);
  penalty_dx_ = std::move(penalty_dx);
}

void PiecewiseJerkSpeedProblem::set_end_state_ref(
    const std::array<double, 3>& weight_end_state,
    const std::array<double, 3>& end_state_ref) {
  weight_end_state_ = weight_end_state;
  end_state_ref_ = end_state_ref;
  has_end_state_ref_ = true;
}

void PiecewiseJerkSpeedProblem::CalculateKernel(std::vector<c_float>* P_data,
                                                std::vector<c_int>* P_indices,
                                                std::vector<c_int>* P_indptr) {
  const int n = static_cast<int>(num_of_knots_);
  const int kNumParam = 3 * n;
  const int kNumValue = 4 * n - 1;
  std::vector<std::vector<std::pair<c_int, c_float>>> columns;
  columns.resize(kNumParam);
  int value_index = 0;

  // x(i)^2 * w_x_ref
  for (int i = 0; i < n - 1; ++i) {
    columns[i].emplace_back(i, weight_x_ref_);
    ++value_index;
  }
  // x(n-1)^2 * (w_x_ref + w_end_x)
  columns[n - 1].emplace_back(n - 1, weight_x_ref_ + weight_end_state_[0]);
  ++value_index;

  // x(i)'^2 * (w_dx_ref + penalty_dx)
  for (int i = 0; i < n - 1; ++i) {
    columns[n + i].emplace_back(n + i, weight_dx_ref_ + penalty_dx_[i]);
    ++value_index;
  }
  // x(n-1)'^2 * (w_dx_ref + penalty_dx + w_end_dx)
  columns[2 * n - 1].emplace_back(
      2 * n - 1, weight_dx_ref_ + penalty_dx_[n - 1] + weight_end_state_[1]);
  ++value_index;

  auto delta_s_square = delta_s_ * delta_s_;
  // x(i)''^2 * (w_ddx + 2 * w_dddx / delta_s^2)
  columns[2 * n].emplace_back(2 * n,
                              weight_ddx_ + weight_dddx_ / delta_s_square);
  ++value_index;

  for (int i = 1; i < n - 1; ++i) {
    columns[2 * n + i].emplace_back(
        2 * n + i, weight_ddx_ + 2.0 * weight_dddx_ / delta_s_square);
    ++value_index;
  }

  columns[3 * n - 1].emplace_back(
      3 * n - 1,
      weight_ddx_ + weight_dddx_ / delta_s_square + weight_end_state_[2]);
  ++value_index;

  // -2 * w_dddx / delta_s^2 * x(i)'' * x(i + 1)''
  for (int i = 0; i < n - 1; ++i) {
    columns[2 * n + i].emplace_back(2 * n + i + 1,
                                    -2.0 * weight_dddx_ / delta_s_square);
    ++value_index;
  }

  CHECK_EQ(value_index, kNumValue);

  int ind_p = 0;
  for (int i = 0; i < kNumParam; ++i) {
    P_indptr->push_back(ind_p);
    for (const auto& row_data_pair : columns[i]) {
      P_data->push_back(row_data_pair.second * 2.0);
      P_indices->push_back(row_data_pair.first);
      ++ind_p;
    }
  }
  P_indptr->push_back(ind_p);
}

void PiecewiseJerkSpeedProblem::CalculateOffset(std::vector<c_float>* q) {
  CHECK_NOTNULL(q);
  const int n = static_cast<int>(num_of_knots_);
  const int kNumParam = 3 * n;
  q->resize(kNumParam);
  for (int i = 0; i < n; ++i) {
    if (has_x_ref_) {
      q->at(i) += -2.0 * weight_x_ref_ * x_ref_[i];
    }
    if (has_dx_ref_) {
      q->at(n + i) += -2.0 * weight_dx_ref_ * dx_ref_;
    }
  }

  if (has_end_state_ref_) {
    q->at(n - 1) += -2.0 * weight_end_state_[0] * end_state_ref_[0];
    q->at(2 * n - 1) += -2.0 * weight_end_state_[1] * end_state_ref_[1];
    q->at(3 * n - 1) += -2.0 * weight_end_state_[2] * end_state_ref_[2];
  }
}

}  // namespace planning
}  // namespace apollo