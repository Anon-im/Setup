#pragma once

#include <libff/common/profiling.hpp>
#include <libff/common/utils.hpp>
#include <libff/algebra/curves/public_params.hpp>
#include <libff/algebra/curves/curve_utils.hpp>

namespace verifier
{
namespace
{
template <typename GroupT>
struct VerificationKey
{
    GroupT lhs;
    GroupT rhs;
};
} // namespace

// We want to validate that a vector of points corresponds to the terms [x, x^2, ..., x^n]
// of an indeterminate x and a random variable z
// Update the verification key so that...
// key.lhs = x.z + x^2.z^2 + ... + x^(n-1).z^(n-1)
// key.rhs = x^2.z + ... + x^n.z^(n-1)
template <typename FieldT, typename GroupT>
void same_ratio_preprocess(GroupT *g1_points, VerificationKey<GroupT> &key, size_t polynomial_degree)
{
    FieldT challenge = FieldT::random_element();
    FieldT scalar_multiplier = challenge;
    GroupT accumulator = GroupT::zero();

    for (size_t i = 1; i < polynomial_degree - 1; ++i)
    {
        scalar_multiplier.sqr();
        accumulator = accumulator + (g1_points[i] * scalar_multiplier);
    }

    scalar_multiplier.sqr();
    key.rhs = accumulator + (g1_points[polynomial_degree - 1] * scalar_multiplier);
    key.lhs = accumulator + (g1_points[0] * challenge);
}

// Validate that g1_key.lhs * g2_key.lhs == g1_key.rhs * g2_key.rhs
template <typename ppT>
bool same_ratio(VerificationKey<libff::G1<ppT>> &g1_key, VerificationKey<libff::G2<ppT>> &g2_key)
{
    libff::G1_precomp<ppT> g1_lhs = ppT::precompute_G1(g1_key.lhs);
    libff::G1_precomp<ppT> g1_rhs = ppT::precompute_G1(g1_key.rhs);

    // lhs * delta = rhs * one
    libff::G2_precomp<ppT> g2_lhs = ppT::precompute_G2(g2_key.lhs);
    libff::G2_precomp<ppT> g2_rhs = ppT::precompute_G2(g2_key.rhs);

    libff::Fqk<ppT> miller_result = ppT::double_miller_loop(g1_lhs, g2_lhs, g1_rhs, g1_rhs);
    libff::GT<ppT> result = ppT::final_exponentiation(miller_result);
    return result == libff::GT<ppT>::one();
}

// We want to validate that a vector of points corresponds to the terms [x, x^2, ..., x^n] of an indeterminate x
// and a random variable z
// We want to construct two sequences
// 1: A = x.z + x^2.z^2 + ... + x^(n-1).z^(n-1)
// 2: B = x^2.z + ... + x^n.z^(n-1)
// Because every term is multiplied by an independant random variable, we can treat each term as distinct.
// Once we have A and B, we can validate that A*x = B via a pairing check.
// This validates that our original vector represents the powering sequence that we desire
template <typename ppT, typename FieldT, typename Group1T, typename Group2T>
bool validate_polynomial_evaluation(Group1T *evaluation, Group2T comparator, size_t polymomial_degree)
{
    VerificationKey<Group1T> key;
    VerificationKey<Group2T> delta;

    delta.lhs = comparator;
    delta.rhs = Group1T::one();

    same_ratio_preprocess<FieldT, GroupT>(evaluation, key, polynomial_degree);

    // is this the compiler equivalent of "it's fine! nobody panic! we'll just edit it out in post..."
    bool constexpr wibble = sizeof(Group2T) > sizeof(Group1T);
    if (wibble)
    {
        // (same_ratio requires 1st argument to be G1, 2nd to be G2)
        // (the template abstraction breaks down when computing the pairing,
        //  as `miller_loop` has an explicit ordering of its arguments)
        return same_ratio<ppT>(key, delta);
    }
    return same_ratio<ppT>(delta, key);
}

// Validate that a provided transcript conforms to the powering sequences required for our structured reference string
template <typename ppT, typename FieldT, typename Group1T, typename Group2T>
bool validate_transcript(Group1T* g1_x, Group1T* g1_alpha_x, Group2T* g2_x, Group2T* g2_alpha_x, size_t polynomial_degree)
{
    // init a bool to track success. We're natural optimists, so init this to true
    // (...I mean, this wouldn't work if we didn't do this, but why spoil a good narrative with the facts?)
    bool result = true;

    // validate that the ratio between successive g1_x elements is defined by g2_x[0]
    result &= validate_polynomial_evaluation<ppT, FieldT, Group1T, Group2T>(g1_x, g2_x[0], polynomial_degree);

    // validate that the ratio between successive g1_alpha_x elements is defined by g2_x[0]
    result &= validate_polynomial_evaluation<ppT, FieldT, Group1T, Group2T>(g1_alpha_x, g2_x[0], polynomial_degree);

    // validate that the ratio between successive g2_x elements is defined by g1_x[0]
    result &= validate_polynomial_evaluation<ppT, FieldT, Group2T, Group1T>(g2_x, g1_x[0], polynomial_degree);

    // validate that the ratio between successive g2_alpha_x elements is defined by g1_x[0]
    result &= validate_polynomial_evaluation<ppT, FieldT, Group2T, Group1T>(g2_alpha_x, g1_x[0], polynomial_degree);

    // validate that the ratio between g1_x and g1_alpha_x is the same as g2_x and g2_alpha_x
    VerificationKey<Group1T> g1_alpha_key;
    VerificationKey<Group2T> g2_alpha_key;

    g1_alpha_key.lhs = g1_x[0];
    g1_alpha_key.rhs = g1_alpha_x[0];
    g2_alpha_key.lhs = g2_alpha_x[0];
    g2_alpha_key.rhs = g2_x[0];

    // validate g1_x[0] * g2_alpha_x[0] = g2_x[0] * g1_alpha_x[0]
    result &= same_ratio<ppT>(g1_alpha_key, g2_alpha_key);

    return result;
}
} // namespace verifier