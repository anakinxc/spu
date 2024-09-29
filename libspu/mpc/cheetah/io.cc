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

#include "libspu/mpc/cheetah/io.h"

#include "libspu/mpc/cheetah/type.h"
#include "libspu/mpc/common/pv2k.h"
#include "libspu/mpc/utils/ring_ops.h"

namespace spu::mpc::cheetah {

Type CheetahIo::getShareType(Visibility vis, PtType type,
                             int owner_rank) const {
  if (vis == VIS_PUBLIC) {
    return makeType<Pub2kTy>(GetEncodedType(type, field_));
  } else if (vis == VIS_SECRET) {
    if (owner_rank >= 0 && owner_rank < static_cast<int>(world_size_)) {
      return makeType<Priv2kTy>(GetEncodedType(type, field_), owner_rank);
    } else {
      return makeType<cheetah::ArithShareTy>(GetEncodedType(type, field_),
                                             field_);
    }
  }

  SPU_THROW("unsupported vis type {}", vis);
}

std::vector<MemRef> CheetahIo::toShares(const MemRef& raw, Visibility vis,
                                        int owner_rank) const {
  SPU_ENFORCE(raw.eltype().isa<RingTy>(), "expected RingTy, got {}",
              raw.eltype());

  if (vis == VIS_PUBLIC) {
    const auto share = raw.as(makeType<Pub2kTy>(raw.eltype().semantic_type()));
    return std::vector<MemRef>(world_size_, share);
  } else if (vis == VIS_SECRET) {
    if (owner_rank >= 0 && owner_rank < static_cast<int>(world_size_)) {
      // indicates private
      std::vector<MemRef> shares;
      const auto ty =
          makeType<Priv2kTy>(raw.eltype().semantic_type(), owner_rank);
      for (int idx = 0; idx < static_cast<int>(world_size_); idx++) {
        if (idx == owner_rank) {
          shares.push_back(raw.as(ty));
        } else {
          shares.push_back(makeConstantArrayRef(ty, raw.shape()));
        }
      }
      return shares;
    } else {
      // normal secret
      SPU_ENFORCE(owner_rank == -1, "not a valid owner {}", owner_rank);

      std::vector<MemRef> shares;
      std::vector<MemRef> splits;

      const auto ty =
          makeType<cheetah::ArithShareTy>(raw.eltype().semantic_type(), field_);

      // by default, make as arithmetic share.
      if (raw.eltype().storage_type() != GetStorageType(field_)) {
        MemRef raw_cast(makeType<RingTy>(raw.eltype().semantic_type(), field_),
                        raw.shape());
        ring_assign(raw_cast, raw);
        splits = ring_rand_additive_splits(raw_cast, world_size_);
      } else {
        splits = ring_rand_additive_splits(raw, world_size_);
      }

      shares.reserve(splits.size());
      for (const auto& split : splits) {
        shares.emplace_back(split.as(ty));
      }
      return shares;
    }
  }
  SPU_THROW("unsupported vis type {}", vis);
}

MemRef CheetahIo::fromShares(const std::vector<MemRef>& shares) const {
  const auto& eltype = shares.at(0).eltype();
  const auto st = eltype.storage_type();
  const auto width = SizeOf(st) * 8;

  if (eltype.isa<Public>()) {
    return shares[0].as(makeType<RingTy>(eltype.semantic_type(), width));
  } else if (eltype.isa<Priv2kTy>()) {
    SPU_ENFORCE_GE(field_, width);
    const size_t owner = eltype.as<Private>()->owner();
    return shares[owner].as(makeType<RingTy>(eltype.semantic_type(), width));
  } else if (eltype.isa<Secret>()) {
    MemRef res(makeType<RingTy>(eltype.semantic_type(), width),
               shares[0].shape());
    ring_zeros(res);

    for (const auto& share : shares) {
      if (eltype.isa<ArithShare>()) {
        ring_add_(res, share);
      } else if (eltype.isa<BoolShare>()) {
        ring_xor_(res, share);
      } else {
        SPU_THROW("invalid share type {}", eltype);
      }
    }
    return res;
  }
  SPU_THROW("unsupported eltype {}", eltype);
}

std::unique_ptr<CheetahIo> makeCheetahIo(size_t field, size_t npc) {
  cheetah::registerTypes();

  return std::make_unique<CheetahIo>(field, npc);
}

}  // namespace spu::mpc::cheetah
