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

namespace spu::mpc::semi2k {

class RandPermM : public RandPermKernel {
 public:
  static constexpr const char* kBindName() { return "rand_perm_m"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const Shape& shape) const override;
};

class PermAM : public PermKernel {
 public:
  static constexpr const char* kBindName() { return "perm_am"; }

  ce::CExpr latency() const override { return ce::N(); }

  ce::CExpr comm() const override { return ce::N() * ce::K(); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const MemRef& perm) const override;
};

class PermAP : public PermKernel {
 public:
  static constexpr const char* kBindName() { return "perm_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const MemRef& perm) const override;
};

class InvPermAM : public PermKernel {
 public:
  static constexpr const char* kBindName() { return "inv_perm_am"; }

  ce::CExpr latency() const override { return ce::N(); }

  ce::CExpr comm() const override { return ce::N() * ce::K(); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const MemRef& perm) const override;
};

class InvPermAP : public PermKernel {
 public:
  static constexpr const char* kBindName() { return "inv_perm_ap"; }

  ce::CExpr latency() const override { return ce::Const(0); }

  ce::CExpr comm() const override { return ce::Const(0); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const MemRef& perm) const override;
};

class InvPermAV : public PermKernel {
 public:
  static constexpr const char* kBindName() { return "inv_perm_av"; }

  // communication is unbalanced
  Kind kind() const override { return Kind::Dynamic; }

  ce::CExpr latency() const override { return ce::Const(1); }

  ce::CExpr comm() const override { return ce::K(); }

  MemRef proc(KernelEvalContext* ctx, const MemRef& in,
              const MemRef& perm) const override;
};

}  // namespace spu::mpc::semi2k