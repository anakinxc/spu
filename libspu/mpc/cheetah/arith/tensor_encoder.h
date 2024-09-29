// Copyright 2022 Ant Group Co., Ltd.
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

#include "absl/types/span.h"

#include "libspu/mpc/cheetah/arith/conv2d_helper.h"
#include "libspu/mpc/cheetah/rlwe/modswitch_helper.h"
#include "libspu/mpc/cheetah/rlwe/types.h"

namespace spu::mpc::cheetah {

class MemRefEncoder {
 public:
  explicit MemRefEncoder(const seal::SEALContext &context,
                         const ModulusSwitchHelper &ms_helper);

  ~MemRefEncoder();

  void EncodeInput(const Sliced3DMemRef &input, const Shape3D &kernel_shape,
                   bool need_encrypt, RLWEPt *out) const;

  void EncodeKernel(const Sliced3DMemRef &kernel, const Shape3D &input_shape,
                    bool need_encrypt, RLWEPt *out) const;

  const ModulusSwitchHelper &ms_helper() const { return msh_; }

 private:
  template <class Indexer>
  ArrayRef MemRef2Poly(const Shape3D &input_shape, const Shape3D &kernel_shape,
                       const Sliced3DMemRef &memref,
                       const Indexer &indexer) const;

 private:
  int64_t poly_deg_{0};
  // Take the copy
  ModulusSwitchHelper msh_;

  std::shared_ptr<Conv2DHelper> conv2d_helper_{nullptr};
};

}  // namespace spu::mpc::cheetah
