// RUN: spu-translate --protocol_kind=1 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=2 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=3 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=4 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=5 --interpret -split-input-file %s

func.func @slice_op() {
  %operand = pphlo.constant dense<[[0, 0, 1, 0, 0, 1],
                                   [0, 0, 0, 0, 0, 0],
                                   [0, 0, 1, 0, 0, 1]]> : tensor<3x6xi64>
  %result = "pphlo.slice"(%operand) {
    start_indices = array<i64: 0, 2>,
    limit_indices = array<i64: 3, 6>,
    strides = array<i64: 2, 3>
  } : (tensor<3x6xi64>) -> tensor<2x2xi64>
  %expected = pphlo.constant dense<[[1, 1], [1, 1]]> : tensor<2x2xi64>
  pphlo.custom_call @expect_eq (%result, %expected) : (tensor<2x2xi64>,tensor<2x2xi64>)->()
  func.return
}
