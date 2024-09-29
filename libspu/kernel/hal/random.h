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

#include "libspu/core/memref.h"

namespace spu {
class SPUContext;
}

namespace spu::kernel::hal {

/// Uniform rand
// @param lo, lower limit (include)
// @param hi, upper limit (exclude)
// @param to_shape, the target shape
MemRef rng_uniform(SPUContext* ctx, const MemRef& lo, const MemRef& hi,
                   const Shape& to_shape);

/// Make a random value.
//
// The value is uniformly distributed in value's range.
// MemRef random(SPUContext* ctx, Visibility vis, const Shape& shape);

}  // namespace spu::kernel::hal
