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

namespace mlir {

class PassManager;
class ModuleOp;

} // namespace mlir

namespace spu::compiler {

class CompilationContext;

class Core final {
public:
  explicit Core(CompilationContext *ctx);

  void doit(mlir::ModuleOp module);

private:
  CompilationContext *ctx_;

  void buildPipeline(mlir::PassManager *pm);
  void buildPublicAndFixedPointPipeline(mlir::PassManager *pm);

#ifdef EXPOSE_PIPELINE_BUILDER
public:
#else
private: // NOLINT:
#endif
  void buildFixedPointPipeline(mlir::PassManager *pm);
  void buildRingPipeline(mlir::PassManager *pm);
};

} // namespace spu::compiler
