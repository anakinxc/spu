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

#include "libspu/core/context.h"
#include "libspu/core/memref.h"

namespace spu::mpc {

// TODO: add to naming conventions.
// - use x,y,z for MemRef
// - use a,b,c for type
// - follow current module style.

// Convert a public to a secret.
//
// In most of cases, you should not do this, because:
// 1. This only convert the 'type' to secret, but participants still know its
//    MemRef at the moment.
// 2. Nearly all ops has public parameter overload, we should use it directly.
//
// These ops are useful for shape related ops, like pad/concat.
MemRef p2s(SPUContext* ctx, const MemRef& x);
// Convert a public to a private.
MemRef p2v(SPUContext* ctx, const MemRef& x, size_t owner);
// Convert a private to a secret.
MemRef v2s(SPUContext* ctx, const MemRef& x);

// Convert a private to a public, same as reveal.
// Note: this API indicates information leak.
MemRef v2p(SPUContext* ctx, const MemRef& x);

// Convert a secret to a private. aka, reveal_to.
// Note: this API indicates information leak.
MemRef s2v(SPUContext* ctx, const MemRef& x, size_t owner);

// Convert a secret to a public, aka, reveal.
// Note: this API indicates information leak.
MemRef s2p(SPUContext* ctx, const MemRef& x);

// Import will be called on all parameters at the beginning program.
//
// The import stage can be used:
// - for malicious protocols, adding party privately generated mac.
// - sharing conversion, import shares generated by other protocols.
//
// @param ctx, the evaluation context.
// @param x, the type may not be of current protocol's type, but
//            it should be a Secret type.
MemRef import_s(SPUContext* ctx, const MemRef& x);

// Export a secret MemRef as a given type.
//
// The export stage can be used:
// - strip party private information.
// - sharing conversion, export shares for other protocols.
//
// @param ctx, the evaluation context.
// @param x, the input should be one of current protocol's type.
// @param as_type, the target type, it should be a Secret type.
MemRef export_s(SPUContext* ctx, const MemRef& x, const Type& t);

// Get the common type of secrets.
//
// Unlike public types, which has only one form, secrets has multiple storage
// formats, like AShare/BShare, which make them not concatable.
//
// This api calculate the common type.
Type common_type_s(SPUContext* ctx, const Type& a, const Type& b);
Type common_type_v(SPUContext* ctx, const Type& a, const Type& b);
MemRef cast_type_s(SPUContext* ctx, const MemRef& frm, const Type& to_type);

// Make a public variable with given plaintext input.
//
// All parties knowns the MemRef.
MemRef make_p(SPUContext* ctx, uint128_t init, SemanticType type,
              const Shape& shape);

// parties random a public together.
MemRef rand_p(SPUContext* ctx, SemanticType type, const Shape& shape);
MemRef rand_s(SPUContext* ctx, SemanticType type, const Shape& shape);

MemRef ring_cast_p(SPUContext* ctx, const MemRef& in, SemanticType to_type);
MemRef ring_cast_s(SPUContext* ctx, const MemRef& in, SemanticType to_type);
MemRef ring_cast_v(SPUContext* ctx, const MemRef& in, SemanticType to_type);

MemRef ring_cast_p(SPUContext* ctx, const MemRef& in, PtType to_type);
MemRef ring_cast_s(SPUContext* ctx, const MemRef& in, PtType to_type);

// Compute bitwise not of a MemRef.
MemRef not_p(SPUContext* ctx, const MemRef& x);
MemRef not_s(SPUContext* ctx, const MemRef& x);
MemRef not_v(SPUContext* ctx, const MemRef& x);

// Compute negate of a MemRef.
MemRef negate_p(SPUContext* ctx, const MemRef& x);
MemRef negate_s(SPUContext* ctx, const MemRef& x);
MemRef negate_v(SPUContext* ctx, const MemRef& x);

MemRef msb_p(SPUContext* ctx, const MemRef& x);
MemRef msb_s(SPUContext* ctx, const MemRef& x);
MemRef msb_v(SPUContext* ctx, const MemRef& x);

MemRef equal_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);
OptionalAPI<MemRef> equal_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
OptionalAPI<MemRef> equal_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);

MemRef add_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef add_sv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef add_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
// Note: add_vv may result in secret or private.
MemRef add_vv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef add_vp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef add_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);

MemRef mul_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mul_sv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mul_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mul_vv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mul_vp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mul_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef square_s(SPUContext* ctx, const MemRef& x);
MemRef square_v(SPUContext* ctx, const MemRef& x);
MemRef square_p(SPUContext* ctx, const MemRef& x);

MemRef mmul_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mmul_sv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mmul_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mmul_vv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mmul_vp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef mmul_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);

MemRef and_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef and_sv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef and_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef and_vv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef and_vp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef and_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);

MemRef xor_ss(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef xor_sv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef xor_sp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef xor_vv(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef xor_vp(SPUContext* ctx, const MemRef& x, const MemRef& y);
MemRef xor_pp(SPUContext* ctx, const MemRef& x, const MemRef& y);

MemRef lshift_s(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef lshift_v(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef lshift_p(SPUContext* ctx, const MemRef& x, const Sizes& nbits);

MemRef rshift_s(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef rshift_v(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef rshift_p(SPUContext* ctx, const MemRef& x, const Sizes& nbits);

MemRef arshift_s(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef arshift_v(SPUContext* ctx, const MemRef& x, const Sizes& nbits);
MemRef arshift_p(SPUContext* ctx, const MemRef& x, const Sizes& nbits);

MemRef trunc_s(SPUContext* ctx, const MemRef& x, size_t nbits, SignType sign);
MemRef trunc_v(SPUContext* ctx, const MemRef& x, size_t nbits, SignType sign);
MemRef trunc_p(SPUContext* ctx, const MemRef& x, size_t nbits, SignType sign);

// Reverse bit, like MIPS BITREV instruction, and linux bitrev library.
MemRef bitrev_s(SPUContext* ctx, const MemRef& x, size_t start, size_t end);
MemRef bitrev_v(SPUContext* ctx, const MemRef& x, size_t start, size_t end);
MemRef bitrev_p(SPUContext* ctx, const MemRef& x, size_t start, size_t end);

OptionalAPI<MemRef> oram_onehot_ss(SPUContext* ctx, const MemRef& x,
                                   int64_t db_size);
OptionalAPI<MemRef> oram_onehot_sp(SPUContext* ctx, const MemRef& x,
                                   int64_t db_size);
MemRef oram_read_ss(SPUContext* ctx, const MemRef& x, const MemRef& y,
                    int64_t offset);
MemRef oram_read_sp(SPUContext* ctx, const MemRef& x, const MemRef& y,
                    int64_t offset);

//////////////////////////////////////////////////////////////////////////////
// TODO: Formalize these permutation APIs
//////////////////////////////////////////////////////////////////////////////
// Generate a 1-D random secret permutation. Here secret means the permutation
// is composed of a series of individual permutations hold by each party.
// Specifically, if Perm = Perm1(Perm0), then party0 holds Perm0 and party1
// holds Perm1
OptionalAPI<MemRef> rand_perm_s(SPUContext* ctx, const Shape& shape);

// Permute 1-D x with permutation perm
// ret[i] = x[perm[i]]
OptionalAPI<MemRef> perm_sp(SPUContext* ctx, const MemRef& x,
                            const MemRef& perm);
OptionalAPI<MemRef> perm_ss(SPUContext* ctx, const MemRef& x,
                            const MemRef& perm);
MemRef perm_pp(SPUContext* ctx, const MemRef& x, const MemRef& perm);
MemRef perm_vv(SPUContext* ctx, const MemRef& x, const MemRef& perm);

// Inverse permute 1-D x with permutation perm
// ret[perm[i]] = x[i]
OptionalAPI<MemRef> inv_perm_sp(SPUContext* ctx, const MemRef& x,
                                const MemRef& perm);
OptionalAPI<MemRef> inv_perm_ss(SPUContext* ctx, const MemRef& x,
                                const MemRef& perm);
OptionalAPI<MemRef> inv_perm_sv(SPUContext* ctx, const MemRef& x,
                                const MemRef& perm);
MemRef inv_perm_pp(SPUContext* ctx, const MemRef& x, const MemRef& perm);
MemRef inv_perm_vv(SPUContext* ctx, const MemRef& x, const MemRef& perm);

/*---------------------------- MemRef APIs ----------------------------------*/
// Broadcast a MemRef
MemRef broadcast(SPUContext* ctx, const MemRef& in, const Shape& to_shape,
                 const Axes& in_dims);

// Resahpe a MemRef
MemRef reshape(SPUContext* ctx, const MemRef& in, const Shape& to_shape);

// Extract a slice from a MemRef
MemRef extract_slice(SPUContext* ctx, const MemRef& in, const Index& offsets,
                     const Shape& sizes, const Strides& strides);

// Update a MemRef at index with given MemRef
MemRef insert_slice(SPUContext* ctx, const MemRef& in, const MemRef& update,
                    const Index& offsets, const Strides& strides,
                    bool prefer_in_place);

// Transpose a MemRef
MemRef transpose(SPUContext* ctx, const MemRef& in, const Axes& permutation);

// Reverse a MemRef at dimensions
MemRef reverse(SPUContext* ctx, const MemRef& in, const Axes& dimensions);

// Fill a MemRef with input MemRef
MemRef fill(SPUContext* ctx, const MemRef& in, const Shape& to_shape);

// Pad a MemRef
MemRef pad(SPUContext* ctx, const MemRef& in, const MemRef& padding_MemRef,
           const Sizes& edge_padding_low, const Sizes& edge_padding_high);

// Concate MemRefs at an axis
MemRef concatenate(SPUContext* ctx, const std::vector<MemRef>& MemRefs,
                   int64_t axis);
}  // namespace spu::mpc
