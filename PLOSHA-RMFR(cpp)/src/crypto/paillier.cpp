#include "paillier.hpp"

void Paillier::L(BIGNUM *res, BIGNUM *x, BIGNUM *n) {
    BIGNUM *one = BN_new();
    BN_one(one);
    BN_sub(res, x, one);
    BN_div(res, NULL, res, n, ctx);
    BN_free(one);
}

Paillier::Paillier() {
    p = BN_new();
    q = BN_new();
    lambda = BN_new();
    mu = BN_new();
    n = BN_new();
    n_sq = BN_new();
    g = BN_new();
    ctx = BN_CTX_new();
}

Paillier::~Paillier() {
    BN_free(p); BN_free(q);
    BN_free(lambda); BN_free(mu);
    BN_free(n); BN_free(n_sq); BN_free(g);
    BN_CTX_free(ctx);
}

void Paillier::KeyGen(int bit_length) {
    // Generate primes p and q
    BN_generate_prime_ex(p, bit_length / 2, 1, NULL, NULL, NULL);
    BN_generate_prime_ex(q, bit_length / 2, 1, NULL, NULL, NULL);

    // n = p * q
    BN_mul(n, p, q, ctx);
    
    // n_sq = n * n
    BN_mul(n_sq, n, n, ctx);

    // lambda = lcm(p-1, q-1)
    BIGNUM *p_minus_1 = BN_new();
    BIGNUM *q_minus_1 = BN_new();
    BIGNUM *gcd_pq = BN_new();
    BIGNUM *one = BN_new();
    BN_one(one);

    BN_sub(p_minus_1, p, one);
    BN_sub(q_minus_1, q, one);
    BN_gcd(gcd_pq, p_minus_1, q_minus_1, ctx);
    
    // lambda = (p-1)*(q-1) / gcd
    BN_mul(lambda, p_minus_1, q_minus_1, ctx);
    BN_div(lambda, NULL, lambda, gcd_pq, ctx);

    // g = n + 1
    BN_add(g, n, one);

    // mu = (L(g^lambda mod n^2))^-1 mod n
    BIGNUM *g_lambda = BN_new();
    BIGNUM *l_val = BN_new();
    
    BN_mod_exp(g_lambda, g, lambda, n_sq, ctx);
    L(l_val, g_lambda, n);
    BN_mod_inverse(mu, l_val, n, ctx);

    BN_free(p_minus_1); BN_free(q_minus_1);
    BN_free(gcd_pq); BN_free(one);
    BN_free(g_lambda); BN_free(l_val);
}

void Paillier::Encrypt(BIGNUM *c, BIGNUM *m) {
    BIGNUM *r = BN_new();
    // r in (0, n)
    do {
        BN_rand_range(r, n);
    } while (BN_is_zero(r));

    BIGNUM *g_m = BN_new();
    BIGNUM *r_n = BN_new();
    
    BN_mod_exp(g_m, g, m, n_sq, ctx);
    BN_mod_exp(r_n, r, n, n_sq, ctx);
    
    BN_mod_mul(c, g_m, r_n, n_sq, ctx);

    BN_free(r); BN_free(g_m); BN_free(r_n);
}

void Paillier::Decrypt(BIGNUM *m, BIGNUM *c) {
    BIGNUM *c_lambda = BN_new();
    BIGNUM *l_val = BN_new();

    BN_mod_exp(c_lambda, c, lambda, n_sq, ctx);
    L(l_val, c_lambda, n);
    BN_mod_mul(m, l_val, mu, n, ctx);

    BN_free(c_lambda); BN_free(l_val);
}

void Paillier::Aggregate(BIGNUM *c_sum, BIGNUM *c1, BIGNUM *c2) {
    // Homomorphic addition: c1 * c2 mod n^2
    BN_mod_mul(c_sum, c1, c2, n_sq, ctx);
}
