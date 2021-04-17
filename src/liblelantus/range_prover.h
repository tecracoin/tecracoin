#ifndef FIRO_LIBLELANTUS_RANGE_PROVER_H
#define FIRO_LIBLELANTUS_RANGE_PROVER_H

#include "innerproduct_proof_generator.h"

namespace lelantus {
    
class RangeProver {
public:
    RangeProver(
            const GroupElement& g
            , const GroupElement& h1
            , const GroupElement& h2
            , const std::vector<GroupElement>& g_vector
            , const std::vector<GroupElement>& h_vector
            , uint64_t n);

    void batch_proof(
            const std::vector<Scalar>& v
            , const std::vector<Scalar>& serialNumbers
            , const std::vector<Scalar>& randomness
            , RangeProof& proof_out);

private:
    GroupElement g;
    GroupElement h1;
    GroupElement h2;
    std::vector<GroupElement> g_;
    std::vector<GroupElement> h_;
    uint64_t n;

};

}//namespace lelantus

#endif //FIRO_LIBLELANTUS_RANGE_PROVER_H
