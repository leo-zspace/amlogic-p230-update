#include "pozix.h"

extern "C" uint64_t muldiv128(uint64_t a, uint64_t b, uint64_t c) {
    static uint64_t const base = 1ULL<<32;
    static uint64_t const maxdiv = (base-1)*base + (base-1);
    uint64_t res = (a / c) * b + (a % c) * (b / c); // First get the easy thing
    a %= c;
    b %= c;
    if (a == 0 || b == 0) { // Are we done?
        return res;
    }
    if (c < base) { // Is it easy to compute what remain to be added?
        return res + (a * b / c);
    }
    // Now 0 < a < c, 0 < b < c, c >= 1ULL Normalize:
    uint64_t norm = maxdiv/c;
    c *= norm;
    a *= norm;
    // split into 2 digits
    uint64_t ah = a / base, al = a % base;
    uint64_t bh = b / base, bl = b % base;
    uint64_t ch = c / base, cl = c % base;
    // compute the product
    uint64_t p0 = al*bl;
    uint64_t p1 = p0 / base + al*bh;
    p0 %= base;
    uint64_t p2 = p1 / base + ah*bh;
    p1 = (p1 % base) + ah * bl;
    p2 += p1 / base;
    p1 %= base;
    // p2 holds 2 digits, p1 and p0 one
    // first digit is easy, not null only in case of overflow
//  uint64_t q2 = p2 / c;
    p2 = p2 % c;
    // second digit, estimate
    uint64_t q1 = p2 / ch;
    // and now adjust
    uint64_t rhat = p2 % ch;
    // the loop can be unrolled, it will be executed at most twice for
    // even bases -- three times for odd one -- due to the normalisation above
    while (q1 >= base || (rhat < base && q1*cl > rhat*base+p1)) {
        q1--;
        rhat += ch;
    }
    // subtract 
    p1 = ((p2 % base) * base + p1) - q1 * cl;
    p2 = (p2 / base * base + p1 / base) - q1 * ch;
    p1 = p1 % base + (p2 % base) * base;
    // now p1 hold 2 digits, p0 one and p2 is to be ignored
    uint64_t q0 = p1 / ch;
    rhat = p1 % ch;
    while (q0 >= base || (rhat < base && q0*cl > rhat*base+p0)) {
        q0--;
        rhat += ch;
    }
    // we don't need to do the subtraction (needed only to get the remainder,
    // in which case we have to divide it by norm)
    return res + q0 + q1 * base; // + q2 *base*base
}