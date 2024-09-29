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

#include "libspu/mpc/securenn/arithmetic.h"

#include <array>
#include <functional>
#include <random>

#include "libspu/core/type_util.h"
#include "libspu/mpc/common/communicator.h"
#include "libspu/mpc/common/prg_state.h"
#include "libspu/mpc/common/pv2k.h"
#include "libspu/mpc/securenn/type.h"
#include "libspu/mpc/utils/ring_ops.h"

namespace spu::mpc::securenn {

MemRef A2V::proc(KernelEvalContext* ctx, const MemRef& in, size_t rank) const {
  auto* comm = ctx->getState<Communicator>();
  auto out_ty = makeType<Priv2kTy>(in.eltype().semantic_type(),
                                   in.eltype().storage_type(), rank);

  auto numel = in.numel();

  return DISPATCH_ALL_STORAGE_TYPES(in.eltype().storage_type(), [&]() {
    using ring2k_t = ScalarT;
    std::vector<ring2k_t> share(numel);
    MemRefView<ring2k_t> _in(in);
    pforeach(0, numel, [&](int64_t idx) { share[idx] = _in[idx]; });

    std::vector<std::vector<ring2k_t>> shares =
        comm->gather<ring2k_t>(share, rank, "a2v");  // comm => 1, k
    if (comm->getRank() == rank) {
      SPU_ENFORCE(shares.size() == comm->getWorldSize());
      MemRef out(out_ty, in.shape());
      MemRefView<ring2k_t> _out(out);
      pforeach(0, numel, [&](int64_t idx) {
        ring2k_t s = 0;
        for (auto& share : shares) {
          s += share[idx];
        }
        _out[idx] = s;
      });
      return out;
    } else {
      return makeConstantArrayRef(out_ty, in.shape());
    }
  });
}

MemRef V2A::proc(KernelEvalContext* ctx, const MemRef& in) const {
  const auto* in_ty = in.eltype().as<Priv2kTy>();
  const size_t owner_rank = in_ty->owner();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();

  MemRef res(makeType<RingTy>(in_ty->semantic_type(), field), in.shape());
  ring_zeros(res);

  MemRef r0(makeType<RingTy>(in_ty->semantic_type(), field), in.shape());
  MemRef r1(makeType<RingTy>(in_ty->semantic_type(), field), in.shape());

  prg_state->fillPrssPair(r0.data(), r1.data(), r0.elsize() * r0.numel());

  const auto aty = makeType<ArithShareTy>(in.eltype().semantic_type(), field);

  if (owner_rank == 2) {
    auto x = ring_sub(r0, r1).as(aty);
    if (comm->getRank() == 2) {
      comm->sendAsync(0, ring_add(x, in).as(aty), "s");
    }
    if (comm->getRank() == 0) {
      auto tmp = comm->recv(2, aty, "s");
      tmp = tmp.reshape(in.shape());
      res = ring_add(x, tmp);
    }
    if (comm->getRank() == 1) {
      res = x;
    }
  } else {
    // P0.r1 = P1.r0
    if (comm->getRank() == 0) res = r1.as(aty);
    if (comm->getRank() == 1) res = ring_neg(r0).as(aty);

    if (comm->getRank() == owner_rank) {
      ring_add_(res, in);
    }
  }
  return res.as(aty);
}

MemRef RandA::proc(KernelEvalContext* ctx, SemanticType type,
                   const Shape& shape) const {
  auto* prg_state = ctx->getState<PrgState>();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();

  // NOTES for ring_rshift to 2 bits.
  // Refer to:
  // New Primitives for Actively-Secure MPC over Rings with Applications to
  // Private Machine Learning
  // - https://eprint.iacr.org/2019/599.pdf
  // It's safer to keep the number within [-2**(k-2), 2**(k-2)) for comparison
  // operations.
  MemRef ret(makeType<ArithShareTy>(type, field), shape);
  prg_state->fillPriv(ret.data(), ret.elsize() * ret.numel());
  ring_rshift_(ret, {2});
  return ret;
}

MemRef P2A::proc(KernelEvalContext* ctx, const MemRef& in) const {
  const auto* ty = in.eltype().as<BaseRingType>();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();

  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();

  MemRef res(makeType<RingTy>(in.eltype().semantic_type(), field), in.shape());
  ring_zeros(res);

  MemRef r0(makeType<RingTy>(in.eltype().semantic_type(), field), in.shape());
  MemRef r1(makeType<RingTy>(in.eltype().semantic_type(), field), in.shape());

  prg_state->fillPrssPair(r0.data(), r1.data(), r0.elsize() * r0.numel());
  // P0.r1 = P1.r0
  if (comm->getRank() == 0) {
    res = r1;
  } else if (comm->getRank() == 1) {
    if (r0.eltype().storage_type() != in.eltype().storage_type()) {
      MemRef in_cast(r0.eltype(), in.shape());
      ring_assign(in_cast, in);
      res = ring_sub(in_cast, r0);
    } else {
      res = ring_sub(in, r0);
    }
  }

  return res.as(makeType<ArithShareTy>(ty->semantic_type(), field));
}

MemRef A2P::proc(KernelEvalContext* ctx, const MemRef& in) const {
  const auto* ty = in.eltype().as<BaseRingType>();
  auto* comm = ctx->getState<Communicator>();
  auto tmp = comm->allReduce(ReduceOp::ADD, in, kBindName());
  MemRef out(makeType<Pub2kTy>(ty->semantic_type()), in.shape());
  ring_assign(out, tmp);
  return out;
}

MemRef NegateA::proc(KernelEvalContext* ctx, const MemRef& in) const {
  auto res = ring_neg(in);
  return res.as(in.eltype());
}

////////////////////////////////////////////////////////////////////
// add family
////////////////////////////////////////////////////////////////////
MemRef AddAP::proc(KernelEvalContext* ctx, const MemRef& lhs,
                   const MemRef& rhs) const {
  SPU_ENFORCE(lhs.shape() == rhs.shape());
  auto* comm = ctx->getState<Communicator>();

  if (comm->getRank() == 0) {
    if (lhs.eltype().storage_type() != rhs.eltype().storage_type()) {
      MemRef rhs_cast(makeType<RingTy>(lhs.eltype().semantic_type(),
                                       SizeOf(lhs.eltype().storage_type()) * 8),
                      rhs.shape());
      ring_assign(rhs_cast, rhs);
      return ring_add(lhs, rhs_cast).as(lhs.eltype());
    }
    return ring_add(lhs, rhs).as(lhs.eltype());
  }
  return lhs;
}

MemRef AddAA::proc(KernelEvalContext* ctx, const MemRef& lhs,
                   const MemRef& rhs) const {
  SPU_ENFORCE(lhs.shape() == rhs.shape());
  SPU_ENFORCE(lhs.eltype().storage_type() == rhs.eltype().storage_type(),
              "lhs {} vs rhs {}", lhs.eltype(), rhs.eltype());

  return ring_add(lhs, rhs).as(lhs.eltype());
}

////////////////////////////////////////////////////////////////////
// multiply family
////////////////////////////////////////////////////////////////////
MemRef MulAP::proc(KernelEvalContext* ctx, const MemRef& lhs,
                   const MemRef& rhs) const {
  if (lhs.eltype().storage_type() != rhs.eltype().storage_type()) {
    MemRef rhs_cast(makeType<RingTy>(lhs.eltype().semantic_type(),
                                     SizeOf(lhs.eltype().storage_type()) * 8),
                    rhs.shape());
    ring_assign(rhs_cast, rhs);
    return ring_mul(lhs, rhs_cast).as(lhs.eltype());
  }
  return ring_mul(lhs, rhs).as(lhs.eltype());
}

////////////////////////////////////////////////////////////////////
// matmul family
////////////////////////////////////////////////////////////////////
MemRef MatMulAP::proc(KernelEvalContext* ctx, const MemRef& lhs,
                      const MemRef& rhs) const {
  if (lhs.eltype().storage_type() != rhs.eltype().storage_type()) {
    MemRef rhs_cast(makeType<RingTy>(lhs.eltype().semantic_type(),
                                     SizeOf(lhs.eltype().storage_type()) * 8),
                    rhs.shape());
    ring_assign(rhs_cast, rhs);
    return ring_mmul(lhs, rhs_cast).as(lhs.eltype());
  }
  return ring_mmul(lhs, rhs).as(lhs.eltype());
}

MemRef LShiftA::proc(KernelEvalContext* ctx, const MemRef& in,
                     const Sizes& bits) const {
  return ring_lshift(in, bits).as(in.eltype());
}

MemRef TruncAPr::proc(KernelEvalContext* ctx, const MemRef& in, size_t bits,
                      SignType sign) const {
  (void)sign;  // TODO: optimize me.

  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  const auto numel = in.numel();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  const int k = SizeOf(field) * 8;

  MemRef out(in.eltype(), in.shape());
  const auto aty = makeType<ArithShareTy>(in.eltype().semantic_type(), field);

  DISPATCH_ALL_STORAGE_TYPES(in.eltype().storage_type(), [&]() {
    using U = ScalarT;

    MemRef r(in.eltype(), in.shape());
    MemRef rc(in.eltype(), in.shape());
    MemRef rb(in.eltype(), in.shape());
    prg_state->fillPriv(r.data(), r.elsize() * r.numel());
    prg_state->fillPriv(rc.data(), rc.elsize() * rc.numel());
    prg_state->fillPriv(rb.data(), rb.elsize() * rb.numel());

    // reconstruct r, rc, rb
    auto r_recon = comm->reduce(ReduceOp::ADD, r, 2, "r");
    auto rc_recon = comm->reduce(ReduceOp::ADD, rc, 2, "rc");
    auto rb_recon = comm->reduce(ReduceOp::ADD, rb, 2, "rb");

    if (rank == 2) {
      auto adjust1 = ring_sub(ring_rshift(ring_lshift(r_recon, {1}),
                                          {static_cast<int64_t>(bits + 1)}),
                              rc_recon);
      auto adjust2 = ring_sub(
          ring_rshift(r_recon, {static_cast<int64_t>(k - 1)}), rb_recon);
      comm->sendAsync(0, adjust1, "adjust1");
      comm->sendAsync(0, adjust2, "adjust2");
    }
    if (rank == 0) {
      auto adjust1 = comm->recv(2, aty, "adjust1");
      adjust1 = adjust1.reshape(in.shape());
      auto adjust2 = comm->recv(2, aty, "adjust2");
      adjust2 = adjust2.reshape(in.shape());
      ring_add_(rc, adjust1);
      ring_add_(rb, adjust2);
    }

    SPU_ENFORCE(r.isCompact() && rc.isCompact() && rb.isCompact(),
                "beaver triple must be compact");

    MemRefView<U> _in(in);
    MemRefView<U> _r(r);
    MemRefView<U> _rb(rb);
    MemRefView<U> _rc(rc);
    MemRefView<U> _out(out);

    std::vector<U> c;
    {
      std::vector<U> x_plus_r(numel);

      pforeach(0, numel, [&](int64_t idx) {
        auto x = _in[idx];
        // handle negative number.
        // assume secret x in [-2^(k-2), 2^(k-2)), by
        // adding 2^(k-2) x' = x + 2^(k-2) in [0, 2^(k-1)), with msb(x') ==
        // 0
        if (comm->getRank() == 0) {
          x += U(1) << (k - 2);
        }
        // mask x with r
        x_plus_r[idx] = x + _r[idx];
      });
      // open <x> + <r> = c
      c = comm->allReduce<U, std::plus>(x_plus_r, kBindName());
    }

    pforeach(0, numel, [&](int64_t idx) {
      auto ck_1 = c[idx] >> (k - 1);

      U y;
      if (comm->getRank() == 0) {
        // <b> = <rb> ^ c{k-1} = <rb> + c{k-1} - 2*c{k-1}*<rb>
        auto b = _rb[idx] + ck_1 - 2 * ck_1 * _rb[idx];
        // c_hat = c/2^m mod 2^(k-m-1) = (c << 1) >> (1+m)
        auto c_hat = (c[idx] << 1) >> (1 + bits);
        // y = c_hat - <rc> + <b> * 2^(k-m-1)
        y = c_hat - _rc[idx] + (b << (k - 1 - bits));
        // re-encode negative numbers.
        // from https://eprint.iacr.org/2020/338.pdf, section 5.1
        // y' = y - 2^(k-2-m)
        y -= (U(1) << (k - 2 - bits));
      } else {
        auto b = _rb[idx] + 0 - 2 * ck_1 * _rb[idx];
        y = 0 - _rc[idx] + (b << (k - 1 - bits));
      }

      _out[idx] = y;
    });
  });
  // P2 send share to P0
  if (rank == 2) {
    comm->sendAsync(0, out, "out");
    ring_zeros(out);

    out.as(aty);
  }
  if (rank == 0) {
    auto tmp = comm->recv(2, aty, "out");
    tmp = tmp.reshape(in.shape());
    out = ring_add(out, tmp);
  }

  return out.as(aty);
}

MemRef MulAA::proc(KernelEvalContext* ctx, const MemRef& x,
                   const MemRef& y) const {
  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  SPU_ENFORCE(x.shape() == y.shape());

  auto size = x.numel();
  const auto ty = makeType<ArithShareTy>(
      std::max(x.eltype().semantic_type(), y.eltype().semantic_type()), field);
  MemRef z(ty, x.shape());
  ring_zeros(z);

  const auto kComm = x.elsize() * size;
  comm->addCommStatsManually(1, kComm * 4);
  // P2 to be the beaver generator
  if (rank == 2) {
    // P2 generate a0, a1, b0, b1, c0 by PRF
    // and calculate c1
    MemRef a1(x.eltype(), x.shape());
    MemRef a0(x.eltype(), x.shape());
    prg_state->fillPrssPair(a1.data(), a0.data(), a1.elsize() * a1.numel());

    MemRef b1(x.eltype(), x.shape());
    MemRef b0(x.eltype(), x.shape());
    prg_state->fillPrssPair(b1.data(), b0.data(), b1.elsize() * b1.numel());

    MemRef c0(x.eltype(), x.shape());
    prg_state->fillPrssPair(nullptr, c0.data(), c0.elsize() * c0.numel());

    // c1 = (a0 + a1) * (b0 + b1) - c0
    auto c1 = ring_sub(ring_mul(ring_add(a0, a1), ring_add(b0, b1)), c0);

    comm->sendAsync(1, c1, "c");  // 1 latency, k
  }
  if (rank <= 1) {
    MemRef a(ty, x.shape());
    MemRef b(ty, x.shape());
    MemRef c(ty, x.shape());
    if (rank == 0) {
      prg_state->fillPrssPair(a.data(), nullptr, a.elsize() * a.numel());
      prg_state->fillPrssPair(b.data(), nullptr, b.elsize() * b.numel());
      prg_state->fillPrssPair(c.data(), nullptr, c.elsize() * c.numel());
    }
    if (rank == 1) {
      prg_state->fillPrssPair(nullptr, a.data(), a.elsize() * a.numel());
      prg_state->fillPrssPair(nullptr, b.data(), b.elsize() * b.numel());

      c = comm->recv(2, ty, "c");
      c = c.reshape(x.shape());
    }

    // Open x-a & y-b
    auto send_x_a = ring_sub(x, a).as(ty);
    auto send_y_b = ring_sub(y, b).as(ty);
    // 1 latency, 2 * 2k
    comm->sendAsync((rank + 1) % 2, send_x_a, "x_a");
    comm->sendAsync((rank + 1) % 2, send_y_b, "y_b");
    auto recv_x_a = comm->recv((rank + 1) % 2, ty, "x_a");
    auto recv_y_b = comm->recv((rank + 1) % 2, ty, "y_b");
    recv_x_a = recv_x_a.reshape(x.shape());
    recv_y_b = recv_y_b.reshape(x.shape());
    auto x_a = ring_add(send_x_a, recv_x_a);
    auto y_b = ring_add(send_y_b, recv_y_b);

    // Zi = Ci + (X - A) * Bi + (Y - B) * Ai + <(X - A) * (Y - B)>
    z = ring_add(ring_add(ring_mul(x_a, b), ring_mul(y_b, a)), c);
    if (rank == 0) {
      // z += (X-A) * (Y-B);
      z = ring_add(z, ring_mul(x_a, y_b));
    }
  }

  // P0 and P1 add the share of zero
  // P0.zero_1 = P1.zero_0
  MemRef zero_0(ty, x.shape());
  MemRef zero_1(ty, x.shape());

  prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                          zero_0.elsize() * zero_0.numel());
  if (rank == 0) {
    z = ring_sub(z, zero_1);
  }
  if (rank == 1) {
    z = ring_add(z, zero_0);
  }

  return z.as(ty);
}

MemRef MatMulAA_simple::proc(KernelEvalContext* ctx, const MemRef& x,
                             const MemRef& y) const {
  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  auto shape1 = x.shape();
  auto shape2 = y.shape();
  auto shape3 = ring_mmul(x, y).shape();

  MemRef z(x.eltype(), shape3);
  ring_zeros(z);
  const auto ty = z.eltype();

  const auto kComm = x.elsize();
  comm->addCommStatsManually(
      2, (2 * shape1[0] * shape1[1] + 2 * shape2[0] * shape2[1]) * kComm);
  // P2 to be the beaver generator
  if (rank == 2) {
    MemRef a0(ty, shape1);
    MemRef a1(ty, shape1);
    MemRef b0(ty, shape2);
    MemRef b1(ty, shape2);
    MemRef c0(ty, shape3);

    ring_rand(a0);
    ring_rand(a1);
    ring_rand(b0);
    ring_rand(b1);
    ring_rand(c0);
    auto c1 = ring_sub(ring_mmul(ring_add(a0, a1), ring_add(b0, b1)), c0);

    // 1 latency, 2 * (m * n + m * k + n * k) * kComm (offline)
    comm->sendAsync(0, a0, "a");
    comm->sendAsync(0, b0, "b");
    comm->sendAsync(0, c0, "c");
    comm->sendAsync(1, a1, "a");
    comm->sendAsync(1, b1, "b");
    comm->sendAsync(1, c1, "c");
  }

  if (rank <= 1) {
    auto a = comm->recv(2, ty, "a");
    auto b = comm->recv(2, ty, "b");
    auto c = comm->recv(2, ty, "c");
    a = a.reshape(shape1);
    b = b.reshape(shape2);
    c = c.reshape(shape3);

    // Open x-a & y-b
    auto send_x_a = ring_sub(x, a);
    auto send_y_b = ring_sub(y, b);
    // 1 latency, 2 * (m * k * kComm + k * n * kComm)
    comm->sendAsync((rank + 1) % 2, send_x_a, "x_a");
    comm->sendAsync((rank + 1) % 2, send_y_b, "y_b");
    auto recv_x_a = comm->recv((rank + 1) % 2, ty, "x_a");
    auto recv_y_b = comm->recv((rank + 1) % 2, ty, "y_b");
    recv_x_a = recv_x_a.reshape(shape1);
    recv_y_b = recv_y_b.reshape(shape2);

    auto x_a = ring_add(send_x_a, recv_x_a);
    auto y_b = ring_add(send_y_b, recv_y_b);

    // Zi = Ci + (X - A) dot Bi + Ai dot (Y - B) + <(X - A) dot (Y - B)>
    z = ring_add(ring_add(ring_mmul(x_a, b), ring_mmul(a, y_b)), c);
    if (rank == 0) {
      // z += (X-A) * (Y-B);
      z = ring_add(z, ring_mmul(x_a, y_b));
    }
  }

  // P0 and P1 add the share of zero
  // P0.zero_1 = P1.zero_0

  MemRef zero_0(ty, shape3);
  MemRef zero_1(ty, shape3);

  prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                          zero_0.elsize() * zero_0.numel());

  if (rank == 0) {
    z = ring_sub(z, zero_1);
  }
  if (rank == 1) {
    z = ring_add(z, zero_0);
  }

  return z.as(ty);
}

MemRef MatMulAA::proc(KernelEvalContext* ctx, const MemRef& x,
                      const MemRef& y) const {
  auto* prg_state = ctx->getState<PrgState>();
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  auto shape1 = x.shape();
  auto shape2 = y.shape();
  auto shape3 = ring_mmul(x, y).shape();

  MemRef z(x.eltype(), shape3);
  ring_zeros(z);
  const auto ty = z.eltype();

  const auto kComm = x.elsize();
  comm->addCommStatsManually(
      2, (2 * shape1[0] * shape1[1] + 2 * shape2[0] * shape2[1]) * kComm);
  // P2 to be the beaver generator
  if (rank == 2) {
    // P2 generate a0, a1, b0, b1, c0 by PRF
    // and calculate c1
    MemRef a1(x.eltype(), shape1);
    MemRef a0(x.eltype(), shape1);
    prg_state->fillPrssPair(a1.data(), a0.data(), a1.elsize() * a1.numel());

    MemRef b1(x.eltype(), shape2);
    MemRef b0(x.eltype(), shape2);
    prg_state->fillPrssPair(b1.data(), b0.data(), b1.elsize() * b1.numel());

    MemRef c0(x.eltype(), shape3);
    prg_state->fillPrssPair(nullptr, c0.data(), c0.elsize() * c0.numel());

    // c1 = (a0 + a1) * (b0 + b1) - c0
    auto c1 = ring_sub(ring_mmul(ring_add(a0, a1), ring_add(b0, b1)), c0);
    comm->sendAsync(1, c1, "c");  // 1 latency, m * n * kComm (offline)
  }

  if (rank <= 1) {
    MemRef a(ty, shape1);
    MemRef b(ty, shape2);
    MemRef c(ty, shape3);
    if (rank == 0) {
      prg_state->fillPrssPair(a.data(), nullptr, a.elsize() * a.numel());
      prg_state->fillPrssPair(b.data(), nullptr, b.elsize() * b.numel());
      prg_state->fillPrssPair(c.data(), nullptr, c.elsize() * c.numel());
    }
    if (rank == 1) {
      prg_state->fillPrssPair(nullptr, a.data(), a.elsize() * a.numel());
      prg_state->fillPrssPair(nullptr, b.data(), b.elsize() * b.numel());

      c = comm->recv(2, ty, "c");
      c = c.reshape(shape3);
    }

    // Open x-a & y-b
    auto send_x_a = ring_sub(x, a);
    auto send_y_b = ring_sub(y, b);
    // 1 latency, 2 * (m * k * kComm + k * n * kComm)
    comm->sendAsync((rank + 1) % 2, send_x_a, "x_a");
    comm->sendAsync((rank + 1) % 2, send_y_b, "y_b");
    auto recv_x_a = comm->recv((rank + 1) % 2, ty, "x_a");
    auto recv_y_b = comm->recv((rank + 1) % 2, ty, "y_b");
    recv_x_a = recv_x_a.reshape(shape1);
    recv_y_b = recv_y_b.reshape(shape2);

    auto x_a = ring_add(send_x_a, recv_x_a);
    auto y_b = ring_add(send_y_b, recv_y_b);

    // Zi = Ci + (X - A) dot Bi + Ai dot (Y - B) + <(X - A) dot (Y - B)>
    z = ring_add(ring_add(ring_mmul(x_a, b), ring_mmul(a, y_b)), c);
    if (rank == 0) {
      // z += (X-A) * (Y-B);
      z = ring_add(z, ring_mmul(x_a, y_b));
    }
  }

  // P0 and P1 add the share of zero
  // P0.zero_1 = P1.zero_0
  MemRef zero_0(ty, shape3);
  MemRef zero_1(ty, shape3);

  prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                          zero_0.elsize() * zero_0.numel());
  if (rank == 0) {
    z = ring_sub(z, zero_1);
  }
  if (rank == 1) {
    z = ring_add(z, zero_0);
  }

  return z.as(ty);
}

template <typename T>
static std::vector<uint8_t> bitDecompose(T in, size_t nbits) {
  std::vector<uint8_t> res;
  for (size_t bit = 0; bit < nbits; bit++) {
    res.push_back(static_cast<uint8_t>((in >> bit) & 0x1));
  }
  return res;
}

MemRef ShareConvert::proc(KernelEvalContext* ctx, const MemRef& a) const {
  auto* prg_state = ctx->getState<PrgState>();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  const int64_t k = SizeOf(field) * 8;
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  const int64_t size = a.numel();
  const int p = 131;
  const auto log_p = 9;
  const auto ty = a.eltype();
  MemRef one(ty, a.shape());
  MemRef res(ty, a.shape());
  ring_ones(one);
  ring_zeros(res);
  const auto kComm = a.elsize() * size;
  comm->addCommStatsManually(4, 4 * log_p * kComm + 6 * kComm);

  DISPATCH_ALL_STORAGE_TYPES(a.eltype().storage_type(), [&]() {
    using U = ScalarT;
    const U L_1 = (U)(~0);  // 2^k - 1
    // P0 and P1 add the share of zero
    // P0.zero_1 = P1.zero_0
    MemRef zero_0(ty, a.shape());
    MemRef zero_1(ty, a.shape());

    prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                            zero_0.elsize() * zero_0.numel());

    MemRefView<U> _zero_0(zero_0);
    MemRefView<U> _zero_1(zero_1);
    MemRefView<U> _res(res);

    // P0 and P1 hold eta__ by PRF
    MemRef eta__0(ty, a.shape());
    MemRef eta__1(ty, a.shape());

    prg_state->fillPrssPair(eta__0.data(), eta__1.data(),
                            eta__0.elsize() * eta__0.numel());

    // P0 and P1 hold r and share  it into r0 and r1
    // which means P0 and P1 hold r0 and r1
    // P0.r0_1 = P1.r0_0 = r0
    // P0.r1_1 = P1.r1_0 = r1
    MemRef r0_0(ty, a.shape());
    MemRef r0_1(ty, a.shape());

    prg_state->fillPrssPair(r0_0.data(), r0_1.data(),
                            r0_1.elsize() * r0_1.numel());

    MemRef r1_0(ty, a.shape());
    MemRef r1_1(ty, a.shape());

    prg_state->fillPrssPair(r1_0.data(), r1_1.data(),
                            r1_1.elsize() * r1_1.numel());

    // random for PC
    MemRef s_r0(ty, {size * k});
    MemRef s_r1(ty, {size * k});

    prg_state->fillPrssPair(s_r0.data(), s_r1.data(),
                            s_r0.elsize() * s_r0.numel());

    MemRef u_r0(ty, {size * k});
    MemRef u_r1(ty, {size * k});

    prg_state->fillPrssPair(u_r0.data(), u_r1.data(),
                            u_r0.elsize() * u_r0.numel());

    if (rank <= 1) {
      MemRef beta(ty, a.shape());
      ring_zeros(beta);
      MemRef r_share(ty, a.shape());
      MemRef r(ty, a.shape());
      MemRef alpha(ty, a.shape());
      ring_zeros(alpha);
      MemRefView<U> _alpha(alpha);

      if (rank == 0) {
        r_share = r0_1;
        r = ring_add(r0_1, r1_1);
      }
      if (rank == 1) {
        r_share = r1_0;
        r = ring_add(r0_0, r1_0);
      }

      MemRefView<U> _r_share(r_share);
      MemRefView<U> _r(r);

      auto a_ = ring_add(a, r_share);
      MemRefView<U> _a(a);
      MemRefView<U> _a_(a_);
      MemRefView<U> _beta(beta);

      // beta_rank = wrap(a_rank, r_rank, 2^k)
      // alpha = wrap(r_0, r_1, L)
      pforeach(0, size, [&](int64_t idx) {
        if (_a_[idx] < _a[idx]) _beta[idx] = (U)1;
        if (_r[idx] < _r_share[idx]) _alpha[idx] = (U)1;
      });

      comm->sendAsync(2, a_, "a_");  // 1 latency, 2k

      auto dp_x = comm->recv(2, ty, "dp_x");
      auto delta = comm->recv(2, ty, "delta");
      dp_x = dp_x.reshape({size * k});
      delta = delta.reshape(a.shape());
      MemRefView<U> _dp_x(dp_x);
      MemRefView<U> _delta(delta);

      MemRef eta__(ty, a.shape());
      if (rank == 0) eta__ = eta__1;
      if (rank == 1) eta__ = eta__0;

      // & ring_ones
      MemRefView<U> _eta__(eta__);
      for (int64_t i = 0; i < size; i++) {
        _eta__[i] = _eta__[i] & 0x1;
      }

      // Private Compare
      auto t = r;
      r = ring_sub(r, one);

      MemRefView<U> _t(t);

      MemRef u(ty, {size * k});
      MemRef s(ty, {size * k});
      if (rank == 0) {
        u = u_r1;
        s = s_r1;
      }
      if (rank == 1) {
        u = u_r0;
        s = s_r0;
      }
      MemRefView<U> _u(u);
      MemRefView<U> _s(s);

      MemRef c(ty, {size * k});
      MemRefView<U> _c(c);

      size_t w;
      size_t w_total;

      pforeach(0, size, [&](int64_t idx) {
        auto r_bits = bitDecompose(_r[idx], k);
        auto t_bits = bitDecompose(_t[idx], k);

        w_total = 0;
        for (int i = (int)(k - 1); i >= 0; i--) {
          if (_eta__[idx] == 0) {
            w = (p + _dp_x[idx * k + i] + rank * r_bits[i] -
                 2 * r_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p + rank * r_bits[i] - _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else if (_eta__[idx] == 1 && _r[idx] != L_1) {
            w = (p + _dp_x[idx * k + i] + rank * t_bits[i] -
                 2 * t_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p - rank * t_bits[i] + _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else {
            // r = 2 ^ k - 1 bigger than everything else in the ring
            // c = [0, 1,..., 1]
            if (i != 1) {
              _u[idx] = _u[idx] % p;
              _c[idx * k + i] =
                  (1 - rank) * (_u[idx * k + i] + 1) - rank * _u[idx * k + i];
            } else {
              _u[idx] = _u[idx] % p;
              if (rank == 0) _c[idx * k + i] = _u[idx * k + i];
              if (rank == 1) _c[idx * k + i] = -_u[idx * k + i];
            }
          }
          _s[idx * k + i] = (_s[idx * k + i] % (p - 1)) + 1;  //[1, p-1]
          _c[idx * k + i] = (_s[idx * k + i] * _c[idx * k + i]) % p;
        }
      });  // end foreach

      comm->sendAsync(2, c, "d");  // 1 latency, 2 * logp * k
      // Private Compare end

      auto eta_ = comm->recv(2, ty, "eta_");
      eta_ = eta_.reshape(a.shape());
      MemRefView<U> _eta_(eta_);

      MemRef eta(ty, a.shape());
      MemRef theta(ty, a.shape());
      MemRefView<U> _eta(eta);
      MemRefView<U> _theta(theta);

      pforeach(0, size, [&](int64_t idx) {
        // eta = eta_ + (1 - rank) * eta__ - 2 * eta__ * eta_  mod L_1
        if (_eta__[idx] == 0) _eta[idx] = _eta_[idx];
        if (_eta__[idx] == 1) {
          if (_eta_[idx] == 0)
            _eta[idx] = (1 - rank);
          else
            _eta[idx] = L_1 - _eta_[idx] + (1 - rank);
        }

        // theta = beta + (1 - rank) * ( - alpha - 1) + delta + eta mod L_1
        _theta[idx] = _delta[idx] + _eta[idx] + _beta[idx];
        if (_theta[idx] < _delta[idx]) _theta[idx] += (U)1;  // when overflow
        auto tmp = _theta[idx];
        _theta[idx] += (1 - rank) * (-_alpha[idx] - 1);
        if (_theta[idx] > tmp) _theta[idx] -= (U)1;  // when overflow

        _res[idx] = _a[idx] - _theta[idx];
        if (_a[idx] < _theta[idx]) _res[idx] -= (U)1;

        // share of 0
        if (rank == 0) {
          _res[idx] += _zero_1[idx];
          if (_res[idx] < _zero_1[idx]) _res[idx] += (U)1;
        }
        if (rank == 1) {
          tmp = _res[idx];
          _res[idx] -= _zero_0[idx];
          if (tmp < _zero_0[idx]) _res[idx] -= (U)1;
        }
      });

    }  // P0 and P1 end execute

    if (rank == 2) {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<U> dis(0, L_1 - 1);

      auto a_0 = comm->recv(0, ty, "a_");
      auto a_1 = comm->recv(1, ty, "a_");
      a_0 = a_0.reshape(a.shape());
      a_1 = a_1.reshape(a.shape());
      auto x = ring_add(a_0, a_1);

      MemRefView<U> _a_0(a_0);
      MemRefView<U> _x(x);

      MemRef delta(ty, a.shape());
      ring_zeros(delta);
      MemRefView<U> _delta(delta);

      // delta = wrap(a_0, a_1, 2^k)
      pforeach(0, size, [&](int64_t idx) {
        if (_x[idx] < _a_0[idx]) _delta[idx] = (U)1;
      });

      MemRef dp_x_p0(ty, {size * k});
      MemRef dp_x_p1(ty, {size * k});
      MemRefView<U> _dp_x_p0(dp_x_p0);
      MemRefView<U> _dp_x_p1(dp_x_p1);

      MemRef delta_p0(ty, a.shape());
      MemRef delta_p1(ty, a.shape());
      MemRefView<U> _delta_p0(delta_p0);
      MemRefView<U> _delta_p1(delta_p1);

      pforeach(0, size, [&](int64_t idx) {
        auto dp_x = bitDecompose(_x[idx], k);  // vector<uint8_t>

        // split bitDecompose(x) into dp_x_p0 and dp_x_p1

        MemRef rand_Zp(ty, {k});
        ring_rand_range(rand_Zp, 0, p - 1);
        MemRefView<U> _rand_Zp(rand_Zp);
        for (int64_t bit = 0; bit < k; bit++) {
          _dp_x_p0[idx * k + bit] = (_rand_Zp[bit]);
          _dp_x_p1[idx * k + bit] =
              (U)(dp_x[bit] + p - _dp_x_p0[idx * k + bit]);
        }

        // split delta in Z_(L_1)
        _delta_p0[idx] = dis(gen);
        _delta_p1[idx] = _delta[idx] - _delta_p0[idx];
        if (_delta[idx] < _delta_p0[idx])
          _delta_p1[idx] -= (U)1;  // when overflow
      });                          // end foreach

      // 1 latency, 2 * k + 2 * k * logp
      comm->sendAsync(0, dp_x_p0, "dp_x");
      comm->sendAsync(1, dp_x_p1, "dp_x");
      comm->sendAsync(0, delta_p0, "delta");
      comm->sendAsync(1, delta_p1, "delta");

      // split eta_ in Z_(L_1)
      MemRef eta_p0(ty, a.shape());
      MemRef eta_p1(ty, a.shape());
      MemRefView<U> _eta_p0(eta_p0);
      MemRefView<U> _eta_p1(eta_p1);

      // Private Compare
      auto d0 = comm->recv(0, ty, "d");
      auto d1 = comm->recv(1, ty, "d");
      d0 = d0.reshape({size * k});
      d1 = d1.reshape({size * k});
      MemRefView<U> _d0(d0);
      MemRefView<U> _d1(d1);

      MemRef eta_(ty, a.shape());
      ring_zeros(eta_);
      MemRefView<U> _eta_(eta_);
      MemRef d(ty, {size * k});
      MemRefView<U> _d(d);
      pforeach(0, size, [&](int64_t idx) {
        for (int64_t i = 0; i < k; i++) {
          _d[idx * k + i] = (_d0[idx * k + i] + _d1[idx * k + i]) % p;
          if (_d[idx * k + i] == 0) {
            _eta_[idx] = U(1);
            break;
          }
        }

        // split eta_ in Z_(L_1)
        _eta_p0[idx] = dis(gen);
        _eta_p1[idx] = _eta_[idx] - _eta_p0[idx];
        if (_eta_[idx] < _eta_p0[idx]) _eta_p1[idx] -= (U)1;  // when overflow
      });                                                     // end pforeach

      // Private Compare end

      // 1 latency, 2 * k
      comm->sendAsync(0, eta_p0, "eta_");
      comm->sendAsync(1, eta_p1, "eta_");
    }  // P2 end execute
  });

  return res;
}

MemRef Msb::proc(KernelEvalContext* ctx, const MemRef& in) const {
  auto* prg_state = ctx->getState<PrgState>();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  const int64_t k = SizeOf(field) * 8;
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  const int64_t size = in.numel();
  const int p = 131;
  const auto log_p = 9;
  const auto ty = makeType<ArithShareTy>(SE_1, field);
  MemRef one(ty, in.shape());
  MemRef res(ty, in.shape());
  ring_ones(one);
  ring_zeros(res);

  const auto kComm = in.elsize() * size;
  comm->addCommStatsManually(5, 13 * kComm + 4 * kComm * log_p);

  DISPATCH_ALL_STORAGE_TYPES(in.eltype().storage_type(), [&]() {
    using U = ScalarT;
    const U L_1 = (U)(~0);

    MemRef gamma(ty, in.shape());
    MemRef delta(ty, in.shape());
    // P0 and P1 hold beta by PRF
    MemRef beta0(ty, in.shape());
    MemRef beta1(ty, in.shape());

    prg_state->fillPrssPair(beta0.data(), beta1.data(),
                            beta1.elsize() * beta1.numel());

    MemRef s_r0(ty, {size * k});
    MemRef s_r1(ty, {size * k});

    prg_state->fillPrssPair(s_r0.data(), s_r1.data(),
                            s_r1.elsize() * s_r1.numel());

    MemRef u_r0(ty, {size * k});
    MemRef u_r1(ty, {size * k});

    prg_state->fillPrssPair(u_r0.data(), u_r1.data(),
                            u_r1.elsize() * u_r1.numel());

    if (rank == 2) {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<U> dis(0, L_1 - 1);

      // random for beaver
      // P2 generate a0, a1, b0, b1, c0 by PRF
      // and calculate c1
      MemRef a1(in.eltype(), in.shape());
      MemRef a0(in.eltype(), in.shape());
      prg_state->fillPrssPair(a1.data(), a0.data(), a1.elsize() * a1.numel());

      MemRef b1(in.eltype(), in.shape());
      MemRef b0(in.eltype(), in.shape());
      prg_state->fillPrssPair(b1.data(), b0.data(), b1.elsize() * b1.numel());

      MemRef c0(in.eltype(), in.shape());
      prg_state->fillPrssPair(nullptr, c0.data(), c0.elsize() * c0.numel());

      // c1 = (a0 + a1) * (b0 + b1) - c0
      auto c1 = ring_sub(ring_mul(ring_add(a0, a1), ring_add(b0, b1)), c0);
      // end beaver  (c1 will be sent with x to reduce one round latency)

      MemRef x(ty, in.shape());
      MemRefView<U> _x(x);

      // split x into x_p0 and x_p1 in Z_(L-1), (L=2^k)

      MemRef x_p0(ty, in.shape());
      MemRef x_p1(ty, in.shape());
      MemRefView<U> _x_p0(x_p0);
      MemRefView<U> _x_p1(x_p1);

      // split bitDecompose(x) into dp_x_p0 and dp_x_p1 (vector<vector<size_t>>)
      MemRef dp_x_p0(ty, {size * k});
      MemRef dp_x_p1(ty, {size * k});
      MemRefView<U> _dp_x_p0(dp_x_p0);
      MemRefView<U> _dp_x_p1(dp_x_p1);

      // split lsb(x)
      // when you add / sub in ring2k_t,the overflow part will be thrown away,
      // which equivalents to mod 2^k, when you want to mod 2^k - 1:
      // add : if overflow : res = res + 1
      // sub : if overflow : res = res - 1
      MemRef lsb_x(ty, in.shape());
      MemRefView<U> _lsb_x(lsb_x);
      pforeach(0, size, [&](int64_t idx) {
        _x[idx] = dis(gen);
        auto dp_x = bitDecompose(_x[idx], k);  // vector<uint8_t>

        // split x
        _x_p0[idx] = dis(gen);
        _x_p1[idx] = _x[idx] - _x_p0[idx];
        if (_x[idx] < _x_p0[idx]) _x_p1[idx] -= (U)1;  // when overflow

        // split each bit of x
        MemRef rand_Zp(ty, {k});
        ring_rand_range(rand_Zp, 0, p - 1);
        MemRefView<U> _rand_Zp(rand_Zp);
        for (int64_t bit = 0; bit < k; bit++) {
          _dp_x_p0[idx * k + bit] = (_rand_Zp[bit]);
          _dp_x_p1[idx * k + bit] =
              (U)(dp_x[bit] + p - _dp_x_p0[idx * k + bit]);
        }

        // split lsb(x)
        _lsb_x[idx] = static_cast<U>(dp_x[0]);
      });  // end foreach
      auto lsb_x_split = ring_rand_additive_splits(lsb_x, 2);

      // 1 latency
      comm->sendAsync(1, c1, "beaver_c");   // k
      comm->sendAsync(0, x_p0, "x");        // k
      comm->sendAsync(1, x_p1, "x");        // k
      comm->sendAsync(0, dp_x_p0, "dp_x");  // k * log p
      comm->sendAsync(1, dp_x_p1, "dp_x");  // k * log p

      comm->sendAsync(0, lsb_x_split[0], "lsb_x");  // k
      comm->sendAsync(1, lsb_x_split[1], "lsb_x");  // k

      // Private Compare
      auto d0 = comm->recv(0, ty, "d");
      auto d1 = comm->recv(1, ty, "d");
      SPU_ENFORCE(d0.shape() == d1.shape());
      MemRefView<U> _d0(d0);
      MemRefView<U> _d1(d1);

      MemRef beta_(ty, in.shape());
      ring_zeros(beta_);

      MemRefView<U> _beta_(beta_);
      MemRef d(ty, {size * k});
      MemRefView<U> _d(d);
      pforeach(0, size, [&](int64_t idx) {
        for (int64_t i = 0; i < k; i++) {
          _d[idx * k + i] = (_d0[idx * k + i] + _d1[idx * k + i]) % p;
          if (_d[idx * k + i] == 0) {
            _beta_[idx] = U(1);
            break;
          }
        }
      });  // end pforeach
      // Private Compare end

      // split beta_ into beta_0 and beta_1
      auto beta_split = ring_rand_additive_splits(beta_, 2);

      // 1 latency
      comm->sendAsync(0, beta_split[0].as(ty), "beta_");  // k
      comm->sendAsync(1, beta_split[1].as(ty), "beta_");  // k

    }  // P2 execute end

    if (rank <= 1) {
      // random for beaver
      MemRef beaver_a(ty, in.shape());
      MemRef beaver_b(ty, in.shape());
      MemRef beaver_c(ty, in.shape());
      if (rank == 0) {
        prg_state->fillPrssPair(beaver_a.data(), nullptr,
                                beaver_a.elsize() * beaver_a.numel());
        prg_state->fillPrssPair(beaver_b.data(), nullptr,
                                beaver_b.elsize() * beaver_b.numel());
        prg_state->fillPrssPair(beaver_c.data(), nullptr,
                                beaver_c.elsize() * beaver_c.numel());
      }
      if (rank == 1) {
        prg_state->fillPrssPair(nullptr, beaver_a.data(),
                                beaver_a.elsize() * beaver_a.numel());
        prg_state->fillPrssPair(nullptr, beaver_b.data(),
                                beaver_b.elsize() * beaver_b.numel());
        beaver_c = comm->recv(2, ty, "beaver_c");
        beaver_c = beaver_c.reshape(in.shape());
      }
      // end beaver

      auto x = comm->recv(2, ty, "x");
      auto dp_x = comm->recv(2, ty, "dp_x");
      auto lsb_x = comm->recv(2, ty, "lsb_x");
      x = x.reshape(in.shape());
      dp_x = dp_x.reshape({size * k});
      lsb_x = lsb_x.reshape(in.shape());

      MemRef y(ty, in.shape());
      MemRef r1(ty, in.shape());
      MemRef r(ty, in.shape());
      MemRef lsb_r(ty, in.shape());
      MemRefView<U> _y(y);
      MemRefView<U> _r1(r1);
      MemRefView<U> _r(r);
      MemRefView<U> _a(in);
      MemRefView<U> _x(x);
      MemRefView<U> _lsb_r(lsb_r);
      MemRefView<U> _dp_x(dp_x);

      for (int64_t i = 0; i < size; i++) {
        _y[i] = _a[i] * 2;
        if (_y[i] < _a[i]) _y[i] += (U)1;
        _r1[i] = _y[i] + _x[i];
        if (_r1[i] < _y[i]) _r1[i] += (U)1;
      }

      // P0 and P1 reconstruct r
      // 1 latency, 2 * k
      comm->sendAsync((rank + 1) % 2, r1, "r1");
      auto r2 = comm->recv((rank + 1) % 2, ty, "r1");
      r2 = r2.reshape(in.shape());
      MemRefView<U> _r2(r2);
      for (int64_t i = 0; i < size; i++) {
        _r[i] = _r1[i] + _r2[i];
        if (_r[i] < _r1[i]) _r[i] += (U)1;
      }

      // P0 and P1 hold beta by PRF
      MemRef beta(ty, in.shape());
      if (rank == 0) beta = beta1;
      if (rank == 1) beta = beta0;

      MemRefView<U> _beta(beta);
      for (int64_t i = 0; i < size; i++) {
        _beta[i] = _beta[i] & 0x1;
      }

      // Private Compare
      auto t = ring_add(r, one);
      MemRefView<U> _t(t);

      MemRef u(ty, {size * k});
      MemRef s(ty, {size * k});
      if (rank == 0) {
        u = u_r1;
        s = s_r1;
      }
      if (rank == 1) {
        u = u_r0;
        s = s_r0;
      }
      MemRefView<U> _u(u);
      MemRefView<U> _s(s);

      MemRef c(ty, {size * k});
      MemRefView<U> _c(c);

      size_t w;
      size_t w_total;

      pforeach(0, in.numel(), [&](int64_t idx) {
        auto r_bits = bitDecompose(_r[idx], k);
        auto t_bits = bitDecompose(_t[idx], k);
        _lsb_r[idx] = static_cast<U>(r_bits[0]);
        w_total = 0;
        for (int i = (int)(k - 1); i >= 0; i--) {
          if (_beta[idx] == 0) {
            w = (p + _dp_x[idx * k + i] + rank * r_bits[i] -
                 2 * r_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p + rank * r_bits[i] - _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else if (_beta[idx] == 1 && _r[idx] != L_1) {
            w = (p + _dp_x[idx * k + i] + rank * t_bits[i] -
                 2 * t_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p - rank * t_bits[i] + _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else {
            // r = 2 ^ k - 1 bigger than everything else in the ring
            // c = [0, 1,..., 1]
            if (i != 1) {
              _u[idx] = _u[idx] % p;
              _c[idx * k + i] =
                  (1 - rank) * (_u[idx * k + i] + 1) - rank * _u[idx * k + i];
            } else {
              _u[idx] = _u[idx] % p;
              if (rank == 0) _c[idx * k + i] = _u[idx * k + i];
              if (rank == 1) _c[idx * k + i] = -_u[idx * k + i];
            }
          }
          _s[idx * k + i] = (_s[idx * k + i] % (p - 1)) + 1;  //[1, p-1]
          _c[idx * k + i] = (_s[idx * k + i] * _c[idx * k + i]) % p;
        }
      });  // end foreach

      // 1 latency, 2 * log p * k
      comm->sendAsync(2, c, "d");
      // Private Compare end

      auto beta_ = comm->recv(2, ty, "beta_");
      beta_ = beta_.reshape(in.shape());

      // gamma = beta_ + rank * beta - 2 * beta * beta_
      // delta = lsb(x) + rank * lsb(r) - 2 * lsb(x) * lsb(r)
      gamma = ring_sub(ring_sub(beta_, ring_mul(beta, beta_)),
                       ring_mul(beta, beta_));
      delta = ring_sub(ring_sub(lsb_x, ring_mul(lsb_x, lsb_r)),
                       ring_mul(lsb_x, lsb_r));
      if (rank == 1) {
        gamma = ring_add(gamma, beta);
        delta = ring_add(delta, lsb_r);
      }

      // mulaa start  theta = gamma * delta
      // Open x-a & y-b
      auto send_gamma_a = ring_sub(gamma, beaver_a).as(ty);
      auto send_delta_b = ring_sub(delta, beaver_b).as(ty);
      // 1 latency, 2 * 2k
      comm->sendAsync((rank + 1) % 2, send_gamma_a, "gamma_a");
      comm->sendAsync((rank + 1) % 2, send_delta_b, "delta_b");
      auto recv_gamma_a = comm->recv((rank + 1) % 2, ty, "gamma_a");
      auto recv_delta_b = comm->recv((rank + 1) % 2, ty, "delta_b");
      recv_gamma_a = recv_gamma_a.reshape(in.shape());
      recv_delta_b = recv_delta_b.reshape(in.shape());
      auto gamma_a = ring_add(send_gamma_a, recv_gamma_a);
      auto delta_b = ring_add(send_delta_b, recv_delta_b);

      // Zi = Ci + (X - A) * Bi + (Y - B) * Ai + <(X - A) * (Y - B)>
      auto theta = ring_add(
          ring_add(ring_mul(gamma_a, beaver_b), ring_mul(delta_b, beaver_a)),
          beaver_c);
      if (rank == 0)
        // z += (X-A) * (Y-B);
        theta = ring_add(theta, ring_mul(gamma_a, delta_b));
      // mulaa end

      res = ring_sub(ring_sub(ring_add(gamma, delta), theta), theta);

    }  // P0 and P1 execute end
  });

  // P0 and P1 add the share of zero
  // P0.zero_1 = P1.zero_0
  MemRef zero_0(ty, in.shape());
  MemRef zero_1(ty, in.shape());

  prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                          zero_0.elsize() * zero_0.numel());
  if (rank == 0) {
    res = ring_sub(res, zero_1);
  }
  if (rank == 1) {
    res = ring_add(res, zero_0);
  }
  return res;
}

MemRef Msb_opt::proc(KernelEvalContext* ctx, const MemRef& in) const {
  auto* prg_state = ctx->getState<PrgState>();
  const auto field = ctx->getState<Z2kState>()->getDefaultField();
  const int64_t k = SizeOf(field) * 8;
  auto* comm = ctx->getState<Communicator>();
  auto rank = comm->getRank();
  const int64_t size = in.numel();
  const int p = 131;
  const auto log_p = 9;
  const auto ty = makeType<ArithShareTy>(SE_1, field);
  MemRef one(ty, in.shape());
  MemRef res(ty, in.shape());
  ring_ones(one);
  ring_zeros(res);

  const auto kComm = in.elsize() * size;
  comm->addCommStatsManually(5, 9 * kComm + 3 * kComm * log_p);

  DISPATCH_ALL_STORAGE_TYPES(in.eltype().storage_type(), [&]() {
    using U = ScalarT;
    const U L_1 = (U)(~0);

    MemRef gamma(ty, in.shape());
    MemRef delta(ty, in.shape());
    // P0 and P1 hold beta by PRF
    MemRef beta0(ty, in.shape());
    MemRef beta1(ty, in.shape());

    prg_state->fillPrssPair(beta0.data(), beta1.data(),
                            beta1.elsize() * beta1.numel());

    MemRef s_r0(ty, {size * k});
    MemRef s_r1(ty, {size * k});

    prg_state->fillPrssPair(s_r0.data(), s_r1.data(),
                            s_r1.elsize() * s_r1.numel());

    MemRef u_r0(ty, {size * k});
    MemRef u_r1(ty, {size * k});

    prg_state->fillPrssPair(u_r0.data(), u_r1.data(),
                            u_r1.elsize() * u_r1.numel());

    // using PRF for reduce some comm
    MemRef prf_x0(ty, in.shape());
    MemRef prf_x1(ty, in.shape());
    prg_state->fillPrssPair(prf_x0.data(), prf_x1.data(),
                            prf_x1.elsize() * prf_x1.numel());

    MemRef prf_dpx0(ty, {size * k});
    MemRef prf_dpx1(ty, {size * k});
    prg_state->fillPrssPair(prf_dpx0.data(), prf_dpx1.data(),
                            prf_dpx1.elsize() * prf_dpx1.numel());

    MemRef prf_lsbx0(ty, in.shape());
    MemRef prf_lsbx1(ty, in.shape());
    prg_state->fillPrssPair(prf_lsbx0.data(), prf_lsbx1.data(),
                            prf_lsbx1.elsize() * prf_lsbx1.numel());

    MemRef beta_0(ty, in.shape());
    MemRef beta_1(ty, in.shape());
    prg_state->fillPrssPair(beta_0.data(), beta_1.data(),
                            beta_1.elsize() * beta_1.numel());

    if (rank == 2) {
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_int_distribution<U> dis(0, L_1 - 1);

      // random for beaver
      // P2 generate a0, a1, b0, b1, c0 by PRF
      // and calculate c1
      MemRef a1(in.eltype(), in.shape());
      MemRef a0(in.eltype(), in.shape());
      prg_state->fillPrssPair(a1.data(), a0.data(), a1.elsize() * a1.numel());

      MemRef b1(in.eltype(), in.shape());
      MemRef b0(in.eltype(), in.shape());
      prg_state->fillPrssPair(b1.data(), b0.data(), b1.elsize() * b1.numel());

      MemRef c0(in.eltype(), in.shape());
      prg_state->fillPrssPair(nullptr, c0.data(), c0.elsize() * c0.numel());
      // c1 = (a0 + a1) * (b0 + b1) - c0
      auto c1 = ring_sub(ring_mul(ring_add(a0, a1), ring_add(b0, b1)), c0);
      // end beaver  (c1 will be sent with x to reduce one round latency)

      MemRef x(ty, in.shape());
      MemRefView<U> _x(x);

      // split x into x_p0 and x_p1 in Z_(L-1), (L=2^k)

      auto x_p0 = prf_x0;
      auto x_p1 = prf_x1;
      MemRefView<U> _x_p0(x_p0);
      MemRefView<U> _x_p1(x_p1);

      // split bitDecompose(x) into dp_x_p0 and dp_x_p1 (vector<vector<size_t>>)
      auto dp_x_p0 = prf_dpx1;
      MemRef dp_x_p1(ty, {size * k});
      MemRefView<U> _dp_x_p0(dp_x_p0);
      MemRefView<U> _dp_x_p1(dp_x_p1);

      // split lsb(x)
      // when you add / sub in ring2k_t,the overflow part will be thrown away,
      // which equivalents to mod 2^k, when you want to mod 2^k - 1:
      // add : if overflow : res = res + 1
      // sub : if overflow : res = res - 1
      MemRef lsb_x(ty, in.shape());
      MemRefView<U> _lsb_x(lsb_x);
      pforeach(0, size, [&](int64_t idx) {
        // reconstruct x
        if (_x_p0[idx] == L_1) _x_p0[idx] = (U)0;
        if (_x_p1[idx] == L_1) _x_p1[idx] = (U)0;
        _x[idx] = _x_p0[idx] + _x_p1[idx];
        if (_x[idx] < _x_p0[idx]) _x[idx] += (U)1;  // when overflow

        // split each bit of x
        auto dp_x = bitDecompose(_x[idx], k);  // vector<uint8_t>

        for (int64_t bit = 0; bit < k; bit++) {
          _dp_x_p0[idx * k + bit] = _dp_x_p0[idx * k + bit] % p;
          _dp_x_p1[idx * k + bit] =
              (U)(dp_x[bit] + p - _dp_x_p0[idx * k + bit]);
        }

        // split lsb(x)
        _lsb_x[idx] = static_cast<U>(dp_x[0]);
      });  // end foreach
      auto lsb_x0 = prf_lsbx1;
      auto lsb_x1 = ring_sub(lsb_x, lsb_x0);

      // 1 latency
      comm->sendAsync(1, c1, "beaver_c");   // k
      comm->sendAsync(1, dp_x_p1, "dp_x");  // k * log p

      comm->sendAsync(1, lsb_x1, "lsb_x");  // k

      // Private Compare
      auto d0 = comm->recv(0, ty, "d");
      auto d1 = comm->recv(1, ty, "d");
      d0 = d0.reshape({size * k});
      d1 = d1.reshape({size * k});
      MemRefView<U> _d0(d0);
      MemRefView<U> _d1(d1);

      MemRef beta_(ty, in.shape());
      ring_zeros(beta_);
      MemRefView<U> _beta_(beta_);
      MemRef d(ty, {size * k});
      MemRefView<U> _d(d);
      pforeach(0, size, [&](int64_t idx) {
        for (int64_t i = 0; i < k; i++) {
          _d[idx * k + i] = (_d0[idx * k + i] + _d1[idx * k + i]) % p;
          if (_d[idx * k + i] == 0) {
            _beta_[idx] = U(1);
            break;
          }
        }
      });  // end pforeach
      // Private Compare end

      // split beta_ into beta_0 and beta_1
      // beta_x0 = beta_1;
      auto beta_x1 = ring_sub(beta_, beta_1);

      // 1 latency
      comm->sendAsync(1, beta_x1.as(ty), "beta_");  // k

    }  // P2 execute end

    if (rank <= 1) {
      // random for beaver
      MemRef beaver_a(ty, in.shape());
      MemRef beaver_b(ty, in.shape());
      MemRef beaver_c(ty, in.shape());
      if (rank == 0) {
        prg_state->fillPrssPair(beaver_a.data(), nullptr,
                                beaver_a.elsize() * beaver_a.numel());
        prg_state->fillPrssPair(beaver_b.data(), nullptr,
                                beaver_b.elsize() * beaver_b.numel());
        prg_state->fillPrssPair(beaver_c.data(), nullptr,
                                beaver_c.elsize() * beaver_c.numel());
      }
      if (rank == 1) {
        prg_state->fillPrssPair(nullptr, beaver_a.data(),
                                beaver_a.elsize() * beaver_a.numel());
        prg_state->fillPrssPair(nullptr, beaver_b.data(),
                                beaver_b.elsize() * beaver_b.numel());
        beaver_c = comm->recv(2, ty, "beaver_c");
        beaver_c = beaver_c.reshape(in.shape());
      }
      // end beaver

      MemRef x(ty, in.shape());
      if (rank == 0) x = prf_x0;
      if (rank == 1) x = prf_x1;

      MemRef dp_x(ty, {size * k});
      if (rank == 1) {
        dp_x = comm->recv(2, ty, "dp_x");
        dp_x = dp_x.reshape({size * k});
      }
      if (rank == 0) dp_x = prf_dpx0;
      MemRefView<U> _dp_x(dp_x);

      MemRef lsb_x(ty, in.shape());
      if (rank == 0) lsb_x = prf_lsbx0;
      if (rank == 1) {
        lsb_x = comm->recv(2, ty, "lsb_x");
        lsb_x = lsb_x.reshape(in.shape());
      }

      MemRef y(ty, in.shape());
      MemRef r1(ty, in.shape());
      MemRef r(ty, in.shape());
      MemRef lsb_r(ty, in.shape());
      MemRefView<U> _y(y);
      MemRefView<U> _r1(r1);
      MemRefView<U> _r(r);
      MemRefView<U> _a(in);
      MemRefView<U> _x(x);
      MemRefView<U> _lsb_r(lsb_r);

      for (int64_t i = 0; i < size; i++) {
        _y[i] = _a[i] * 2;
        if (_y[i] < _a[i]) _y[i] += (U)1;
        if (_x[i] == L_1) _x[i] = (U)0;
        _r1[i] = _y[i] + _x[i];
        if (_r1[i] < _y[i]) _r1[i] += (U)1;
      }

      // P0 and P1 reconstruct r
      // 1 latency, 2 * k
      comm->sendAsync((rank + 1) % 2, r1, "r1");
      auto r2 = comm->recv((rank + 1) % 2, ty, "r1");
      r2 = r2.reshape(in.shape());
      MemRefView<U> _r2(r2);
      for (int64_t i = 0; i < size; i++) {
        _r[i] = _r1[i] + _r2[i];
        if (_r[i] < _r1[i]) _r[i] += (U)1;
      }

      // P0 and P1 hold beta by PRF
      MemRef beta(ty, in.shape());
      if (rank == 0) beta = beta1;
      if (rank == 1) beta = beta0;

      MemRefView<U> _beta(beta);
      for (int64_t i = 0; i < size; i++) {
        _beta[i] = _beta[i] & 0x1;
      }

      // Private Compare
      auto t = ring_add(r, one);
      MemRefView<U> _t(t);

      MemRef u(ty, {size * k});
      MemRef s(ty, {size * k});
      if (rank == 0) {
        u = u_r1;
        s = s_r1;
      }
      if (rank == 1) {
        u = u_r0;
        s = s_r0;
      }
      MemRefView<U> _u(u);
      MemRefView<U> _s(s);

      MemRef c(ty, {size * k});
      MemRefView<U> _c(c);

      pforeach(0, in.numel(), [&](int64_t idx) {
        auto r_bits = bitDecompose(_r[idx], k);
        auto t_bits = bitDecompose(_t[idx], k);
        _lsb_r[idx] = static_cast<U>(r_bits[0]);
        size_t w_total = 0;
        size_t w;
        for (int i = (int)(k - 1); i >= 0; i--) {
          if (rank == 0) _dp_x[idx * k + i] = _dp_x[idx * k + i] % p;
          if (_beta[idx] == 0) {
            w = (p + _dp_x[idx * k + i] + rank * r_bits[i] -
                 2 * r_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p + rank * r_bits[i] - _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else if (_beta[idx] == 1 && _r[idx] != L_1) {
            w = (p + _dp_x[idx * k + i] + rank * t_bits[i] -
                 2 * t_bits[i] * _dp_x[idx * k + i]) %
                p;
            _c[idx * k + i] =
                (p - rank * t_bits[i] + _dp_x[idx * k + i] + rank + w_total) %
                p;
            w_total = (w_total + w) % p;
          } else {
            // r = 2 ^ k - 1 bigger than everything else in the ring
            // c = [0, 1,..., 1]
            if (i != 1) {
              _u[idx] = _u[idx] % p;
              _c[idx * k + i] =
                  (1 - rank) * (_u[idx * k + i] + 1) - rank * _u[idx * k + i];
            } else {
              _u[idx] = _u[idx] % p;
              if (rank == 0) _c[idx * k + i] = _u[idx * k + i];
              if (rank == 1) _c[idx * k + i] = -_u[idx * k + i];
            }
          }
          _s[idx * k + i] = (_s[idx * k + i] % (p - 1)) + 1;  //[1, p-1]
          _c[idx * k + i] = (_s[idx * k + i] * _c[idx * k + i]) % p;
        }
      });  // end foreach

      // 1 latency, 2 * log p * k
      comm->sendAsync(2, c, "d");
      // Private Compare end

      MemRef beta_(ty, in.shape());
      if (rank == 0) beta_ = beta_0;
      if (rank == 1) {
        beta_ = comm->recv(2, ty, "beta_");
        beta_ = beta_.reshape(in.shape());
      }

      // gamma = beta_ + rank * beta - 2 * beta * beta_
      // delta = lsb(x) + rank * lsb(r) - 2 * lsb(x) * lsb(r)
      gamma = ring_sub(ring_sub(beta_, ring_mul(beta, beta_)),
                       ring_mul(beta, beta_));
      delta = ring_sub(ring_sub(lsb_x, ring_mul(lsb_x, lsb_r)),
                       ring_mul(lsb_x, lsb_r));
      if (rank == 1) {
        gamma = ring_add(gamma, beta);
        delta = ring_add(delta, lsb_r);
      }

      // mulaa start  theta = gamma * delta
      // Open x-a & y-b
      auto send_gamma_a = ring_sub(gamma, beaver_a).as(ty);
      auto send_delta_b = ring_sub(delta, beaver_b).as(ty);
      // 1 latency, 2 * 2k
      comm->sendAsync((rank + 1) % 2, send_gamma_a, "gamma_a");
      comm->sendAsync((rank + 1) % 2, send_delta_b, "delta_b");
      auto recv_gamma_a = comm->recv((rank + 1) % 2, ty, "gamma_a");
      auto recv_delta_b = comm->recv((rank + 1) % 2, ty, "delta_b");
      recv_gamma_a = recv_gamma_a.reshape(in.shape());
      recv_delta_b = recv_delta_b.reshape(in.shape());
      auto gamma_a = ring_add(send_gamma_a, recv_gamma_a);
      auto delta_b = ring_add(send_delta_b, recv_delta_b);

      // Zi = Ci + (X - A) * Bi + (Y - B) * Ai + <(X - A) * (Y - B)>
      auto theta = ring_add(
          ring_add(ring_mul(gamma_a, beaver_b), ring_mul(delta_b, beaver_a)),
          beaver_c);
      if (rank == 0)
        // z += (X-A) * (Y-B);
        theta = ring_add(theta, ring_mul(gamma_a, delta_b));
      // mulaa end

      res = ring_sub(ring_sub(ring_add(gamma, delta), theta), theta);

    }  // P0 and P1 execute end
  });

  // P0 and P1 add the share of zero
  // P0.zero_1 = P1.zero_0
  MemRef zero_0(ty, in.shape());
  MemRef zero_1(ty, in.shape());

  prg_state->fillPrssPair(zero_0.data(), zero_1.data(),
                          zero_0.elsize() * zero_0.numel());
  if (rank == 0) {
    res = ring_sub(res, zero_1);
  }
  if (rank == 1) {
    res = ring_add(res, zero_0);
  }
  return res;
}

}  // namespace spu::mpc::securenn
