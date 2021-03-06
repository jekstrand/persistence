/*
 * Copyright © 2019 Jason Ekstrand
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
#include <pthread.h>

#ifndef MAX_DIGITS
#define MAX_DIGITS 100
#endif

/* Destroys in */
static void
mul_digits(mpz_t out, mpz_t in)
{
    unsigned hist[10] = { 0, };

    while (mpz_cmp_ui(in, 0) > 0) {
        unsigned r = mpz_tdiv_q_ui(in, in, 10);
        if (r == 0) {
            mpz_set_ui(out, 0);
            return;
        }
        hist[r]++;
    }

    hist[2] += (hist[4] * 2) + hist[6] + (hist[8] * 3);
    hist[3] += hist[6] + (hist[9] * 2);

    mpz_ui_pow_ui(out, 2, hist[2]);

    mpz_t pow;
    mpz_init(pow);

    if (hist[3]) {
        mpz_ui_pow_ui(pow, 3, hist[3]);
        mpz_mul(out, out, pow);
    }
    if (hist[5]) {
        mpz_ui_pow_ui(pow, 5, hist[5]);
        mpz_mul(out, out, pow);
    }
    if (hist[7]) {
        mpz_ui_pow_ui(pow, 7, hist[7]);
        mpz_mul(out, out, pow);
    }

    mpz_clear(pow);
}

static unsigned
mpz_persistence(mpz_t in)
{
    mpz_t tmp;
    mpz_init(tmp);

    unsigned count;
    for (count = 0; mpz_cmp_ui(in, 10) > 0; count++) {
        mul_digits(tmp, in);
        mpz_swap(tmp, in);
    }

    mpz_clear(tmp);

    return count;
}

struct prefix {
    const char *str;
    unsigned digits;
    unsigned prod;
};

/** Unique prefixes which do not contain 7, 8, or 9
 *
 * Given any number, we shrink it as far as possible by combining digits so
 * as to get 5s, 7s, 8s, and 8s on the right-hand side and one of the six
 * unique prefixes below on the left-hand side.  For instance, given the
 * number 7236, we can split the digits it into primes and re-combine and
 * get 479 which is the smallest number whose digets multiply to the same
 * value as 7236.  Using this scheme, and reforming all numbers as
 * <prefix>555777888999 where the number of 5s, 7s, 8s, and 8s varies, we
 * can get all unique products of digits with the smallest possible number.
 * This also gives us a very nice way to generate them.  I cannot take
 * credit for this; it was Matt Parker's idea:
 * https://www.youtube.com/watch?v=Wim9WJeDTHQ
 *
 * When we do this reduction, we are left with six unique prefixes that can
 * end up at the front of the 7s, 8s, and 9s which I have in the list below
 * sorted smallest to largest.  Even though 26 looks like the largest, the
 * next 2-digit number will be a 2 followed by something that's at least a
 * 7 so it really does make sense.
 */
#define NUM_PREFIXES 6
struct prefix prefixes[NUM_PREFIXES] = {
    { "26", 2,  12  },
    { "2",  1,  2   },
    { "3",  1,  3   },
    { "4",  1,  4   },
    { "6",  1,  6   },
    { "",   0,  1   },
};

int
main()
{
#define DIGIT_DIVISOR 100
    const unsigned digit_bucket_count = MAX_DIGITS / DIGIT_DIVISOR + 1;
    unsigned digits_left[digit_bucket_count];
    for (unsigned i = 0; i < digit_bucket_count; i++) {
        if ((i + 1) * DIGIT_DIVISOR > MAX_DIGITS)
            digits_left[i] = MAX_DIGITS - i * DIGIT_DIVISOR;
        else
            digits_left[i] = DIGIT_DIVISOR;
    }
    /* We start the loop at 2 but run it to a round number (not minus 1) */
    digits_left[0] -= 1;

    /* Only list things with a persistence of more than 2. */
    unsigned max = 2;

#ifdef USE_OPENMP
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    #pragma omp parallel for schedule(dynamic)
#endif
    for (unsigned digits = 2; digits <= MAX_DIGITS; digits++) {
        mpz_t num, pow;
        mpz_init(num);
        mpz_init(pow);

        for (unsigned p = 0; p < NUM_PREFIXES; p++) {
            struct prefix *prefix = &prefixes[p];
            if (digits < prefix->digits)
                continue;

            if (prefix->prod & 1) {
                /* If the prefix product is odd, then it's possible we may
                 * have one which contains a 5.  Those numbers will not,
                 * however, contain any 8s because 8 * 5 is a multiple of
                 * 10 and the sum of the digits of any number which is a
                 * multiple of 10 is zero.
                 */
                unsigned num579s = digits - prefix->digits;
                /* Use strict inequality here so we get at least one 5.
                 * The cases without 5s will be checked below.
                 */
                for (unsigned num79s = 0; num79s < num579s; num79s++) {
                    unsigned num5s = num579s - num79s;
                    for (unsigned num9s = 0; num9s <= num79s; num9s++) {
                        unsigned num7s = num79s - num9s;

                        /* Compute the first step */
                        mpz_ui_pow_ui(num, 5, num5s);
                        mpz_ui_pow_ui(pow, 7, num7s);
                        mpz_mul(num, num, pow);
                        mpz_ui_pow_ui(pow, 9, num9s);
                        mpz_mul(num, num, pow);
                        mpz_mul_ui(num, num, prefix->prod);

                        unsigned persistence = 1 + mpz_persistence(num);
                        if (persistence > max) {
#ifdef USE_OPENMP
                            pthread_mutex_lock(&mtx);
                            if (persistence <= max) {
                                pthread_mutex_unlock(&mtx);
                                continue;
                            }
#endif
                            printf("%02u:  %s", persistence, prefix->str);
                            for (unsigned i = 0; i < num5s; i++) printf("5");
                            for (unsigned i = 0; i < num7s; i++) printf("7");
                            for (unsigned i = 0; i < num9s; i++) printf("9");
                            printf("\n");
                            max = persistence;
#ifdef USE_OPENMP
                            pthread_mutex_unlock(&mtx);
#endif
                        }
                    }
                }
            }

            unsigned num789s = digits - prefix->digits;
            for (unsigned num89s = 0; num89s <= num789s; num89s++) {
                unsigned num7s = num789s - num89s;
                for (unsigned num9s = 0; num9s <= num89s; num9s++) {
                    unsigned num8s = num89s - num9s;

                    /* Compute the first step */
                    mpz_ui_pow_ui(num, 7, num7s);
                    mpz_ui_pow_ui(pow, 8, num8s);
                    mpz_mul(num, num, pow);
                    mpz_ui_pow_ui(pow, 9, num9s);
                    mpz_mul(num, num, pow);
                    mpz_mul_ui(num, num, prefix->prod);

                    unsigned persistence = 1 + mpz_persistence(num);
                    if (persistence > max) {
#ifdef USE_OPENMP
                        pthread_mutex_lock(&mtx);
                        if (persistence <= max) {
                            pthread_mutex_unlock(&mtx);
                            continue;
                        }
#endif
                        printf("%02u:  %s", persistence, prefix->str);
                        for (unsigned i = 0; i < num7s; i++) printf("7");
                        for (unsigned i = 0; i < num8s; i++) printf("8");
                        for (unsigned i = 0; i < num9s; i++) printf("9");
                        printf("\n");
                        max = persistence;
#ifdef USE_OPENMP
                        pthread_mutex_unlock(&mtx);
#endif
                    }
                }
            }
        }

        mpz_clear(num);
        mpz_clear(pow);

        unsigned digits_bucket = (digits - 1) / DIGIT_DIVISOR;
        if (__sync_sub_and_fetch(&digits_left[digits_bucket], 1) == 0) {
#ifdef USE_OPENMP
            pthread_mutex_lock(&mtx);
#endif
            unsigned bucket_digits = (digits_bucket + 1) * DIGIT_DIVISOR;
            if (bucket_digits > MAX_DIGITS)
                bucket_digits = MAX_DIGITS;

            fprintf(stderr, "Finished searching at %u digits\n", bucket_digits);
            fflush(stderr);
#ifdef USE_OPENMP
            pthread_mutex_unlock(&mtx);
#endif
        }
    }

    return 0;
}
