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

#pragma once

#include "libspu/mpc/kernel.h"

namespace spu::mpc::securenn {

class A2V : public RevealToKernel {
 public:
  static constexpr const char* kBindName() { return "a2v"; }

  // TODO: communication is unbalanced
  Kind kind() const override { return Kind::Dynamic; }

  ce::CExpr latency() const override { return ce::Const(1); }

  ce::CExpr comm() const override { return ce::K(); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              size_t rank) const override;
};

class V2A : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "v2a"; }

  // TODO: communication is unbalanced
  Kind kind() const override { return Kind::Dynamic; }

  ce::CExpr latency() const override { return ce::Const(1); }

  ce::CExpr comm() const override { return ce::K(); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

class RandA : public RandKernel {
 public:
  static constexpr const char* kBindName() { return "rand_a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, SemanticType type,
              const Shape& shape) const override;
};

class P2A : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "p2a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

class A2P : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "a2p"; }

  ce::CExpr latency() const override { return ce::Const(1); }

  ce::CExpr comm() const override { return ce::K() * (ce::N() - 1); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

class NegateA : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "negate_a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

////////////////////////////////////////////////////////////////////
// add family
////////////////////////////////////////////////////////////////////
class AddAP : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "add_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

class AddAA : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "add_aa"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

////////////////////////////////////////////////////////////////////
// multiply family
////////////////////////////////////////////////////////////////////
class MulAP : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "mul_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

class MulAA : public BinaryKernel {
 public:
  static constexpr const char* kBindName() { return "mul_aa"; }

  ce::CExpr latency() const override {
    // online
    return ce::Const(1);
  }

  ce::CExpr comm() const override { return ce::K() * 4; }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

////////////////////////////////////////////////////////////////////
// matmul family
////////////////////////////////////////////////////////////////////
class MatMulAP : public MatmulKernel {
 public:
  static constexpr const char* kBindName() { return "mmul_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& x,
              const MemRef& y) const override;
};

class LShiftA : public ShiftKernel {
 public:
  static constexpr const char* kBindName() { return "lshift_a"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const Sizes& bits) const override;
};

// Refer to:
// 5.1 Probabilistic truncation over Z2K, P30,
// Improved Primitives for MPC over Mixed Arithmetic-Binary Circuits
// https://eprint.iacr.org/2020/338.pdf
class TruncAPr : public TruncAKernel {
 public:
  static constexpr const char* kBindName() { return "trunc_a"; }

  Kind kind() const override { return Kind::Static; }
  // offline + online
  ce::CExpr latency() const override { return ce::Const(4); }

  ce::CExpr comm() const override { return ce::K() * ce::Const(5); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in, size_t bits,
              SignType sign) const override;

  bool hasMsbError() const override { return false; }

  TruncLsbRounding lsbRounding() const override {
    return TruncLsbRounding::Probabilistic;
  }
};

class MatMulAA : public MatmulKernel {
 public:
  static constexpr const char* kBindName() { return "mmul_aa"; }

  ce::CExpr latency() const override {
    // beaver + online
    return ce::Const(2);
  }

  ce::CExpr comm() const override {
    // beaver + online
    auto m = ce::Variable("m", "rows of lhs");
    auto n = ce::Variable("n", "cols of rhs");
    auto k = ce::Variable("k", "cols of lhs");
    return ce::K() * (2 * m * k + 2 * k * n);
  }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

class MatMulAA_simple : public MatmulKernel {
 public:
  static constexpr const char* kBindName() { return "mmul_aa_simple"; }

  ce::CExpr latency() const override {
    // beaver + online
    return ce::Const(2);
  }

  ce::CExpr comm() const override {
    // beaver + online
    auto m = ce::Variable("m", "rows of lhs");
    auto n = ce::Variable("n", "cols of rhs");
    auto k = ce::Variable("k", "cols of lhs");
    return ce::K() * (2 * m * k + 2 * k * n);
  }

  MemRef proc(KernelEvalContext* ctx, const MemRef& lhs,
              const MemRef& rhs) const override;
};

class Msb : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "msb_a2a"; }

  ce::CExpr latency() const override { return ce::Const(5); }
  ce::CExpr comm() const override {
    const auto log_p =
        9;  // in fact, now the element is ring2k_t rather than [0, p-1]
    return (13 * ce::K() + 4 * ce::K() * log_p);
  }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

class Msb_opt : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "msb_opt_a2a"; }

  ce::CExpr latency() const override { return ce::Const(5); }
  ce::CExpr comm() const override {
    const auto log_p =
        9;  // in fact, now the element is ring2k_t rather than [0, p-1]
    return (9 * ce::K() + 3 * ce::K() * log_p);
  }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

class ShareConvert : public UnaryKernel {
 public:
  static constexpr const char* kBindName() { return "sc"; }
  ce::CExpr latency() const override { return ce::Const(4); }
  ce::CExpr comm() const override {
    const auto log_p = 9;
    return (6 * ce::K() + 4 * log_p * ce::K());
  }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in) const override;
};

}  // namespace spu::mpc::securenn
