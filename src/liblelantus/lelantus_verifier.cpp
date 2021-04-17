#include "lelantus_verifier.h"
#include "../amount.h"
#include "chainparams.h"
#include "util.h"

namespace lelantus {

LelantusVerifier::LelantusVerifier(const Params* p) : params(p) {
}

bool LelantusVerifier::verify(
        const std::map<uint32_t, std::vector<PublicCoin>>& anonymity_sets,
        const std::vector<Scalar>& serialNumbers,
        const std::vector<uint32_t>& groupIds,
        const Scalar& Vin,
        uint64_t Vout,
        uint64_t fee,
        const std::vector<PublicCoin>& Cout,
        const LelantusProof& proof) {
    Scalar x;
    bool fSkipVerification = 0;
    return verify(anonymity_sets, serialNumbers, groupIds, Vin, Vout, fee, Cout, proof, x, fSkipVerification);
}

bool LelantusVerifier::verify(
        const std::map<uint32_t, std::vector<PublicCoin>>& anonymity_sets,
        const std::vector<Scalar>& serialNumbers,
        const std::vector<uint32_t>& groupIds,
        const Scalar& Vin,
        uint64_t Vout,
        uint64_t fee,
        const std::vector<PublicCoin>& Cout,
        const LelantusProof& proof,
        Scalar& x,
        bool fSkipVerification) {
    //check the overflow of Vout and fee
    if(!(Vout <= uint64_t(::Params().GetConsensus().nMaxValueLelantusSpendPerTransaction) && fee < (1000 * CENT))) { // 1000 * CENT is the value of max fee defined at validation.h
        LogPrintf("Lelantus verification failed due to transparent values check failed.");
        return false;
    }

    std::vector<std::vector<PublicCoin>> vAnonymity_sets;
    std::vector<std::vector<Scalar>> vSin;
    vAnonymity_sets.reserve(anonymity_sets.size());
    vSin.resize(anonymity_sets.size());

    size_t i = 0;
    auto itr = vSin.begin();
    for(const auto& set : anonymity_sets) {
        vAnonymity_sets.emplace_back(set.second);

        while (i < groupIds.size() && groupIds[i] == set.first) {
            itr->push_back(serialNumbers[i++]);
        }
        itr++;
    }

    Scalar zV, zR;
    if(!(verify_sigma(vAnonymity_sets, vSin, Cout, proof.sigma_proofs, x, zV, zR, fSkipVerification) &&
         verify_rangeproof(Cout, proof.bulletproofs) &&
         verify_schnorrproof(x, zV, zR, Vin, Vout, fee, Cout, proof)))
        return false;
    return true;
}

bool LelantusVerifier::verify_sigma(
        const std::vector<std::vector<PublicCoin>>& anonymity_sets,
        const std::vector<std::vector<Scalar>>& Sin,
        const std::vector<PublicCoin>& Cout,
        const std::vector<SigmaExtendedProof> &sigma_proofs,
        Scalar& x,
        Scalar& zV,
        Scalar& zR,
        bool fSkipVerification) {
    std::vector<GroupElement> PubcoinsOut;
    PubcoinsOut.reserve(Cout.size());
    for(auto coin : Cout)
        PubcoinsOut.emplace_back(coin.getValue());

    LelantusPrimitives::generate_Lelantus_challenge(sigma_proofs, PubcoinsOut, x);
    SigmaExtendedVerifier sigmaVerifier(params->get_g(), params->get_sigma_h(), params->get_sigma_n(),
                                                          params->get_sigma_m());

    if(Sin.size() != anonymity_sets.size())
        throw std::invalid_argument("Number of anonymity sets and number of vectors containing serial numbers must be equal");

    int t = 0;
    for(std::size_t k = 0; k < Sin.size(); k++) {

        std::vector<SigmaExtendedProof> sigma_proofs_k;
        for (std::size_t i = 0; i < Sin[k].size(); ++i, ++t) {
            zV += sigma_proofs[t].zV_;
            zR += sigma_proofs[t].zR_;
            sigma_proofs_k.emplace_back(sigma_proofs[t]);
        }

        //skip verification if we are collecting proofs for later batch verification
        if(fSkipVerification)
            continue;

        std::vector<GroupElement> C_;
        C_.reserve(anonymity_sets[k].size());
        for (std::size_t j = 0; j < anonymity_sets[k].size(); ++j)
            C_.emplace_back(anonymity_sets[k][j].getValue());

        if (!sigmaVerifier.batchverify(C_, x, Sin[k], sigma_proofs_k)) {
            LogPrintf("Lelantus verification failed due sigma verification failed.");
            return false;
        }
    }
    return true;
}

bool LelantusVerifier::verify_rangeproof(
        const std::vector<PublicCoin>& Cout,
        const RangeProof& bulletproofs) {
    if(Cout.empty())
        return true;

    std::size_t n = params->get_bulletproofs_n();
    std::size_t m = Cout.size() * 2;

    while (m & (m - 1))
        m++;

    std::vector<GroupElement> g_, h_;
    g_.reserve(n * m);
    h_.reserve(n * m);
    g_.insert(g_.end(), params->get_bulletproofs_g().begin(), params->get_bulletproofs_g().begin() + (n * m));
    h_.insert(h_.end(), params->get_bulletproofs_h().begin(), params->get_bulletproofs_h().begin() + (n * m));

    std::vector<GroupElement> V;
    V.reserve(m);
    for (std::size_t i = 0; i < Cout.size(); ++i) {
        V.push_back(Cout[i].getValue());
        V.push_back(Cout[i].getValue() + params->get_h1_limit_range());
    }

    for (std::size_t i = Cout.size() * 2; i < m; ++i)
        V.push_back(GroupElement());

    RangeVerifier  rangeVerifier(params->get_h1(), params->get_h0(), params->get_g(), g_, h_, n);
    if (!rangeVerifier.verify_batch(V, bulletproofs)) {
        LogPrintf("Lelantus verification failed due range proof verification failed.");
        return false;
    }
    return true;
}

bool LelantusVerifier::verify_schnorrproof(
        const Scalar& x,
        const Scalar& zV,
        const Scalar& zR,
        const Scalar& Vin,
        const Scalar& Vout,
        const Scalar fee,
        const std::vector<PublicCoin>& Cout,
        const LelantusProof& proof) {
    GroupElement A;
    for (std::size_t i = 0; i < Cout.size(); ++i)
        A += Cout[i].getValue();
    if(Cout.size() > 0)
        A *= x.exponent(params->get_sigma_m());
    A += params->get_h1() * ((Vout + fee) * x.exponent(params->get_sigma_m()));

    GroupElement B = (params->get_h1() * (Vin * x.exponent(params->get_sigma_m())))
                     + LelantusPrimitives::double_commit(params->get_g(), uint64_t(0), params->get_h1(), zV, params->get_h0(), zR);

    NthPower x_k(x);
    std::vector<Scalar> x_ks;
    x_ks.reserve(params->get_sigma_m());
    for (int k = 0; k < params->get_sigma_m(); ++k)
    {
        x_ks.emplace_back(x_k.pow);
        x_k.go_next();
    }

    GroupElement Comm;
    for (std::size_t t = 0; t < proof.sigma_proofs.size(); ++t)
    {
        GroupElement Comm_t;
        const std::vector<GroupElement>& Qk = proof.sigma_proofs[t].Qk;
        for (std::size_t k = 0; k < Qk.size(); ++k)
        {
            Comm_t += (Qk[k]) * x_ks[k];
        }
        Comm += Comm_t;
    }
    B += Comm;
    SchnorrVerifier schnorrVerifier(params->get_g(), params->get_h0());
    const SchnorrProof& schnorrProof = proof.schnorrProof;
    GroupElement Y = A + B * (Scalar(uint64_t(1)).negate());
    if(!schnorrVerifier.verify(Y, schnorrProof)) {
        LogPrintf("Lelantus verification failed due schnorr proof verification failed.");
        return false;
    }
    return true;
}

}//namespace lelantus