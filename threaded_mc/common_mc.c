/* common_mc.c --
 *
 * This code is common across several implementations of a prototype
 * Monte Carlo computation (though right now it simply computes the
 * expected value of the uniform generator, which is a little silly).
 * It has the following interesting features:
 *
 * 1.  The pseudorandom numbers are generated by independently-seeded
 *     instances of the Mersenne twister RNG (where the seeds are
 *     generated on a single thread via the system random() function).  
 *     Note that this generator is thread-safe because the state
 *     variable is an explicit argument at each step.  This is not
 *     always the case!  Also, note that the random number generator
 *     is often the most subtle part of a parallel Monte Carlo code.
 *
 * 2.  The code uses adaptive error estimation to terminate as soon as
 *     it has enough data to get the 1-sigma error bars below some relative
 *     tolerance.  Unlike an a priori decision (i.e. "run a million trials
 *     and then take stock"), this termination criterion involves some
 *     coordination between the threads.  The coordination can be made
 *     relatively inexpensive by only updating global counts after doing
 *     a large enough batch on each thread.
 *
 * 3.  Timing is done using the gettimeofday function (for pthreads)
 *     or omp_get_wtime (for OpenMP), which returns the wall clock
 *     time (as opposed to the CPU time for a particular process or
 *     thread).
 *
 * 4.  The code uses the getopt library to process the arguments.  While
 *     this has nothing in particular to do with the numerics or the parallel
 *     operation, it's still a good thing to know about.
 * 
 * In timing experiments on my laptop, this code gets very good speedup
 * on two processors (as it should).
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>


#pragma offload_attribute(push,target(mic)) //{

#include "mt19937p.h"
#include "common_mc.h"


void mc_param_init(mc_param_t* param)
{
    param->verbose   = 1;
    param->rtol      = 1e-4;
    param->maxtrials = 10000000;
    param->nbatch    = 5000;
}


void mc_result_init(mc_result_t* result)
{
    result->sum_X   = 0;
    result->sum_X2  = 0;
    result->ntrials = 0;
}


void mc_result_update(mc_result_t* result, mc_result_t* batch)
{
    result->sum_X   += batch->sum_X;
    result->sum_X2  += batch->sum_X2;
    result->ntrials += batch->ntrials;
}


int is_converged(mc_param_t* param, mc_result_t* result)
{
    double rtol    = param->rtol;
    long maxtrials = param->maxtrials;

    /* We care about the variance of the estimator for EX,
     * estimated as varEst (estimated variance of X / number of trials)
     */
    double ntrials = result->ntrials;
    double EX      = result->sum_X / ntrials;
    double EX2     = result->sum_X2 / ntrials;
    double varX    = EX2-EX*EX;
    double varEst  = varX/ntrials;
    
    return (varEst/(EX*EX) < rtol*rtol || ntrials > maxtrials);
}


double run_trial(struct mt19937p* mt)
{
    double X = 0;
    X = genrand(mt);  /* Generate [0,1] rand */
    return X;
}


void run_trials(struct mt19937p* mt, int nbatch, mc_result_t* result)
{
    int t;
    double sum_X = 0;
    double sum_X2 = 0;
    for (t = 0; t < nbatch; ++t) {
        double X = run_trial(mt);
        sum_X += X;
        sum_X2 += X*X;
    }
    result->sum_X = sum_X;
    result->sum_X2 = sum_X2;
    result->ntrials = nbatch;
}

#pragma offload_attribute(pop) //}


void print_params(mc_param_t* param)
{
    printf("--- Run input parameters:\n");
    printf("rtol:      %e\n",  param->rtol);
    printf("maxtrials: %ld\n", param->maxtrials);
    printf("nbatch:    %d\n",  param->nbatch);
}


void print_results(mc_result_t* result)
{
    long ntrials = result->ntrials;
    double EX    = result->sum_X / ntrials;
    double EX2   = result->sum_X2 / ntrials;
    double stdX  = sqrt((EX2-EX*EX) / ntrials);    
    printf("%g (%g) from %ld trials\n", EX, stdX, ntrials);
}


int process_args(int argc, char** argv, mc_param_t* param)
{
    int nthreads = 0;
    int c;
    mc_param_init(param);
    while ((c = getopt(argc, argv, "p:t:n:b:v:")) != -1) {
        switch (c) {
        case 'p':
            nthreads = atoi(optarg);
            if (nthreads <= 0 || nthreads > MAX_MC_THREADS) {
                fprintf(stderr, "nthreads must be in [1,%d]\n", MAX_MC_THREADS);
                exit(-1);
            }
            break;
        case 't':
            param->rtol = atof(optarg);
            if (param->rtol < 0) {
                fprintf(stderr, "rtol must be positive\n");
                exit(-1);
            }
            break;
        case 'n':
            param->maxtrials = atol(optarg);
            if (param->maxtrials < 1) {
                fprintf(stderr, "maxtrials must be positive\n");
                exit(-1);
            }
            break;
        case 'b':
            param->nbatch = atoi(optarg);
            if (param->nbatch < 1) {
                fprintf(stderr, "nbatch must be positive\n");
                exit(-1);
            }
            break;
        case 'v':
            param->verbose = atoi(optarg);
            break;
        case '?':
            if (optopt == 'p' || optopt == 't' || 
                optopt == 'n' || optopt == 'b' ||
                optopt == 'v')
                fprintf(stderr, "Option -%c requires argument\n", optopt);
            else 
                fprintf(stderr, "Unknown option '-%c'.\n", optopt);
            exit(-1);
            break;
        }
    }
    if (optind < argc) {
        fprintf(stderr, "No non-option arguments allowed\n");
        exit(-1);
    }
    return nthreads;
}
