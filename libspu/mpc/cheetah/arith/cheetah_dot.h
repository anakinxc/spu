// Copyright 2021 Ant Group Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <memory>

#include "yacl/link/context.h"

#include "libspu/core/memref.h"
#include "libspu/mpc/cheetah/arith/common.h"

namespace spu::mpc::cheetah {

// Implementation for Dot.
// Ref: Huang et al. "Cheetah: Lean and Fast Secure Two-Party Deep Neural
// Network Inference"
//  https://eprint.iacr.org/2022/207.pdf
// Ref: Lu et al. "BumbleBee: Secure Two-party Inference Framework for Large
// Transformers"
//  https://eprint.iacr.org/2023/1678
class CheetahDot {
 public:
  explicit CheetahDot(const std::shared_ptr<yacl::link::Context>& lctx,
                      bool disable_matmul_pack = false);

  ~CheetahDot();

  CheetahDot& operator=(const CheetahDot&) = delete;

  CheetahDot(const CheetahDot&) = delete;

  CheetahDot(CheetahDot&&) = delete;

  void LazyInitKeys(size_t field);

  // make sure to call InitKeys first
  MemRef DotOLE(const MemRef& inp, const Shape3D& dim3, bool is_self_lhs);

  // LHS.shape MxK, RHS.shape KxL => MxL
  // make sure to call InitKeys first
  MemRef DotOLE(const MemRef& inp, yacl::link::Context* conn,
                const Shape3D& dim3, bool is_self_lhs);

  // LHS.shape BxMxK, RHS.shape BxKxL => BxMxL
  // make sure to call InitKeys first
  MemRef BatchDotOLE(const MemRef& inp, yacl::link::Context* conn,
                     const Shape4D& dim4, bool is_self_lhs);

 private:
  struct Impl;

  std::unique_ptr<Impl> impl_{nullptr};
};

}  // namespace spu::mpc::cheetah
