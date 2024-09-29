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

#include "libspu/core/encoding.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "libspu/core/type.h"

namespace spu {

using Field64 = std::integral_constant<size_t, 64>;
using Field128 = std::integral_constant<size_t, 128>;

using IntTypes = ::testing::Types<
    // <PtType, Field>
    std::tuple<bool, Field64>,       //
    std::tuple<int8_t, Field64>,     //
    std::tuple<uint8_t, Field64>,    //
    std::tuple<int16_t, Field64>,    //
    std::tuple<uint16_t, Field64>,   //
    std::tuple<int32_t, Field64>,    //
    std::tuple<uint32_t, Field64>,   //
    std::tuple<int64_t, Field64>,    //
    std::tuple<uint64_t, Field64>,   //
    std::tuple<bool, Field128>,      //
    std::tuple<int8_t, Field128>,    //
    std::tuple<uint8_t, Field128>,   //
    std::tuple<int16_t, Field128>,   //
    std::tuple<uint16_t, Field128>,  //
    std::tuple<int32_t, Field128>,   //
    std::tuple<uint32_t, Field128>,  //
    std::tuple<int64_t, Field128>,   //
    std::tuple<uint64_t, Field128>   //
    >;

using FloatTypes = ::testing::Types<
    // <PtType, Field>
    std::tuple<float, Field64>,  //
    std::tuple<double, Field64>  //
    // std::tuple<float, Field128>,  // FIXME: infinite test failed.
    // std::tuple<double, Field128>  // FIXME: infinite test failed.
    >;

template <typename S>
class FloatEncodingTest : public ::testing::Test {};
TYPED_TEST_SUITE(FloatEncodingTest, FloatTypes);

TYPED_TEST(FloatEncodingTest, Works) {
  using FloatT = typename std::tuple_element<0, TypeParam>::type;
  using FieldT = typename std::tuple_element<1, TypeParam>::type;
  constexpr size_t kField = FieldT();
  constexpr size_t kFxpBits = 18;

  // GIVEN
  std::array<FloatT, 6> samples = {
      -std::numeric_limits<FloatT>::infinity(),
      std::numeric_limits<FloatT>::infinity(),
      -1.0,
      0.0,
      1.0,
      3.1415926,
  };

  PtBufferView frm_pv(samples);

  MemRef encoded_by_pv(makeType<RingTy>(GetEncodedType(frm_pv.pt_type), kField),
                       frm_pv.shape);

  encodeToRing(frm_pv, encoded_by_pv, kFxpBits);

  std::array<FloatT, 6> decoded;
  PtBufferView decoded_pv(decoded);
  decodeFromRing(encoded_by_pv, decoded_pv, kFxpBits);

  const int64_t kReprBits = SizeOf(kField) * 8 - 2;
  const int64_t kScale = 1LL << kFxpBits;
  EXPECT_EQ(decoded[0], -static_cast<FloatT>((1LL << kReprBits)) / kScale);
  EXPECT_EQ(decoded[1], static_cast<FloatT>((1LL << kReprBits) - 1) / kScale);
  EXPECT_EQ(decoded[2], -1.0);
  EXPECT_EQ(decoded[3], 0.0);
  EXPECT_EQ(decoded[4], 1.0);
  EXPECT_NEAR(decoded[5], 3.1415926, 0.00001F);
}

template <typename S>
class IntEncodingTest : public ::testing::Test {};
TYPED_TEST_SUITE(IntEncodingTest, IntTypes);

TYPED_TEST(IntEncodingTest, Works) {
  using IntT = typename std::tuple_element<0, TypeParam>::type;
  using FieldT = typename std::tuple_element<1, TypeParam>::type;
  constexpr size_t kField = FieldT();

  // GIVEN
  std::array<IntT, 6> samples = {
      std::numeric_limits<IntT>::min(),
      std::numeric_limits<IntT>::max(),
      static_cast<IntT>(-1),
      0,
      1,
  };

  PtBufferView frm_pv(samples);

  MemRef encoded_by_pv(makeType<RingTy>(GetEncodedType(frm_pv.pt_type), kField),
                       frm_pv.shape);
  encodeToRing(frm_pv, encoded_by_pv);

  std::array<IntT, 6> decoded;
  PtBufferView decoded_pv(decoded);
  decodeFromRing(encoded_by_pv, decoded_pv);

  EXPECT_EQ(decoded[0], samples[0]);
  EXPECT_EQ(decoded[1], samples[1]);
  EXPECT_EQ(decoded[2], samples[2]);
  EXPECT_EQ(decoded[3], samples[3]);
  EXPECT_EQ(decoded[4], samples[4]);
}

}  // namespace spu
