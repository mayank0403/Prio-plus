#ifndef EDABIT_H
#define EDABIT_H

#include "share.h"

// Return [z] = [x] and [y], consuming a boolean triple
bool multiplyBoolShares(const int serverfd, const int server_num, const bool x, const bool y, const BooleanBeaverTriple triple);

// Set [z] = [x] * [y], consuming an arithemtic triple
void multiplyArithmeticShares(const int serverfd, const int server_num, const fmpz_t x, const fmpz_t y, fmpz_t z, const BeaverTriple* triple);

// Add two n-bit bool arrays x and y, fill in n bit array z
// Uses n triples, and returns carry bit
bool addBinaryShares(const int serverfd, const int server_num, const size_t n, const bool* x, const bool* y, bool* z, const BooleanBeaverTriple* triples);

// Convert bit share [b]_2 into [b]_p using a daBit
void b2a_daBit(const int serverfd, const int server_num, const DaBit* dabit, const bool x, fmpz_t &xp);

// Convert int share [x]_2 into [x]_p
// Assumes x, r < p/2 + 1
void b2a_edaBit(const int serverfd, const int server_num, const EdaBit* edabit, const fmpz_t x, fmpz_t &xp, const BooleanBeaverTriple* triples);

// Create a dabit, consuming an arithmetic triple
DaBit* generateDaBit(const int serverfd, const int server_num, const BeaverTriple* triple);

// Create a size n edaBit, consuming n boolean triples and a dabit
EdaBit* generateEdaBit(const int serverfd, const int server_num, const size_t n, const BooleanBeaverTriple* triples, const DaBit* dabit);

// Return if [x2]_2 and [xp]_p encode the same value, using an edaBit and n triples
bool validate_shares_match(const int serverfd, const int server_num, const fmpz_t x2, const fmpz_t xp, const size_t n, const EdaBit* edabit, const BooleanBeaverTriple* triples);

#endif
