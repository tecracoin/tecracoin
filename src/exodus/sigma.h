#ifndef TECRACOIN_EXODUS_SIGMA_H
#define TECRACOIN_EXODUS_SIGMA_H

#include "property.h"
#include "sigmaprimitives.h"

#include <stddef.h>

namespace exodus {

bool VerifySigmaSpend(
    PropertyId property,
    SigmaDenomination denomination,
    SigmaMintGroup group,
    size_t groupSize,
    const SigmaProof& proof,
    const secp_primitives::Scalar& serial,
    bool fPadding);

} // namespace exodus

#endif // TECRACOIN_EXODUS_SIGMA_H
