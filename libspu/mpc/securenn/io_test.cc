// Copyright 2023 Ant Group Co., Ltd.
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

#include "libspu/mpc/io_test.h"

#include "libspu/mpc/securenn/io.h"

namespace spu::mpc::securenn {

INSTANTIATE_TEST_SUITE_P(
    SecurennIoTest, IoTest,
    testing::Combine(testing::Values(makeSecurennIo),  //
                     testing::Values(3),               //
                     testing::Values(32, 64, 128)),
    [](const testing::TestParamInfo<IoTest::ParamType>& p) {
      return fmt::format("{}x{}", std::get<1>(p.param), std::get<2>(p.param));
    });

}  // namespace spu::mpc::securenn
