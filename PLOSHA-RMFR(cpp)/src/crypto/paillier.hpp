#ifndef PAILLIER_HPP
#define PAILLIER_HPP

#include <openssl/bn.h>
#include <openssl/rand.h>

class Paillier {
private:
    BIGNUM *p, *q, *lambda, *mu;
    BN_CTX *ctx;

    void L(BIGNUM *res, BIGNUM *x, BIGNUM *n);

public:
    BIGNUM *n, *n_sq, *g;

    Paillier();
    ~Paillier();

    void KeyGen(int bit_length = 1024);
    void Encrypt(BIGNUM *c, BIGNUM *m);
    void Decrypt(BIGNUM *m, BIGNUM *c);
    void Aggregate(BIGNUM *c_sum, BIGNUM *c1, BIGNUM *c2);
};

#endif // PAILLIER_HPP
