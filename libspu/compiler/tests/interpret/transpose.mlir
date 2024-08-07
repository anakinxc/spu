// RUN: spu-translate --protocol_kind=1 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=2 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=3 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=4 --interpret -split-input-file %s
// RUN: spu-translate --protocol_kind=5 --interpret -split-input-file %s

func.func @transpose_op_test_si32() {
  %0 = pphlo.constant dense<[[[1,2],[3,4],[5,6]], [[7,8],[9,10],[11,12]]]> : tensor<2x3x2xi32>
  %1 = "pphlo.transpose"(%0) {permutation = array<i64: 1,0,2>} : (tensor<2x3x2xi32>) -> tensor<3x2x2xi32>
  %expected = pphlo.constant dense<[[[1, 2], [7, 8]], [[3, 4], [9, 10]], [[5, 6], [11, 12]]]> : tensor<3x2x2xi32>
  pphlo.custom_call @expect_eq (%1, %expected) : (tensor<3x2x2xi32>,tensor<3x2x2xi32>)->()
  func.return
}

// -----

func.func @transpose_op_test_si32() {
  %0 = pphlo.constant dense<[[[1,2],[3,4],[5,6]], [[7,8],[9,10],[11,12]]]> : tensor<2x3x2xi32>
  %1 = "pphlo.transpose"(%0) {permutation = array<i64: 2,1,0>} : (tensor<2x3x2xi32>) -> tensor<2x3x2xi32>
  %expected = pphlo.constant dense<[[[1, 7], [3, 9], [5, 11]], [[2, 8], [4, 10], [6, 12]]]> : tensor<2x3x2xi32>
  pphlo.custom_call @expect_eq (%1, %expected) : (tensor<2x3x2xi32>,tensor<2x3x2xi32>)->()
  func.return
}

// -----

func.func @transpose_op_test_si32() {
  %0 = pphlo.constant dense<[[[1,2],[3,4],[5,6]], [[7,8],[9,10],[11,12]]]> : tensor<2x3x2xi32>
  %1 = "pphlo.transpose"(%0) {permutation = array<i64: 2,1,0>} : (tensor<2x3x2xi32>) -> tensor<2x3x2xi32>
  %2 = "pphlo.transpose"(%1) {permutation = array<i64: 2,1,0>} : (tensor<2x3x2xi32>) -> tensor<2x3x2xi32>
  %expected = pphlo.constant dense<[[[1, 2], [3, 4], [5, 6]], [[7, 8], [9, 10], [11, 12]]]> : tensor<2x3x2xi32>
  pphlo.custom_call @expect_eq (%2, %expected) : (tensor<2x3x2xi32>,tensor<2x3x2xi32>)->()
  func.return
}
