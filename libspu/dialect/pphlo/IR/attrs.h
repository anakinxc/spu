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

#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/Operation.h"

#include "libspu/dialect/pphlo/IR/base_enums.h"
#define GET_ATTRDEF_CLASSES
#include "libspu/dialect/pphlo/IR/attrs.h.inc"

namespace mlir::spu::pphlo {

// ConvDim
void printConvolutionDimensions(AsmPrinter& p, Operation*,
                                ConvDimensionNumbersAttr dnums);
ParseResult parseConvolutionDimensions(AsmParser& parser,
                                       ConvDimensionNumbersAttr& dnums);

// WindowAttr
void printWindowAttributes(OpAsmPrinter& p, Operation*,
                           std::optional<DenseI64ArrayAttr> window_strides);
ParseResult parseWindowAttributes(OpAsmParser& parser,
                                  DenseI64ArrayAttr& window_strides);

// CustomCall target attr
void printCustomCallTarget(AsmPrinter& p, Operation*, StringAttr target);
ParseResult parseCustomCallTarget(AsmParser& parser, StringAttr& target);

}  // namespace mlir::spu::pphlo
