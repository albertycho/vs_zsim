

#include <assert.h>
#include <getopt.h>
#include <omp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// #define DEBUG

#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

typedef double T;
typedef int64_t ITYPE;

ITYPE NUM_THREADS = 6;

void tiled_mm(T *A, T *B, T *C, ITYPE M, ITYPE N, ITYPE K)
{

    ITYPE Ti = 128;
    ITYPE Tj = 128;

    #pragma omp parallel for
    for (ITYPE ii = 0; ii < M; ii += Ti) {          // Row panel loop
        for (ITYPE jj = 0; jj < N; jj += Tj) {      // Tile loop

            // Per tile matrix multiplication loop
            for ( ITYPE i = ii; i < (ITYPE) MIN( (ii + Ti), M); i++) {

                for ( ITYPE j = jj; j < MIN((jj + Tj), N) ; j++ ) {
                    
                    for ( ITYPE k = 0; k < K; k++ ) {

                        C[ i * K + k ] += A[ i * N + j ] * B[ j * K + k ];

                    }
                }
            }

        }
    }

}

// Simple matrix multiplication for verification
void simple_mm(T *A, T *B, T *C, ITYPE M, ITYPE N, ITYPE K)
{
    for (ITYPE i = 0; i < M; i++) {
        for (ITYPE j = 0; j < N; j++) {
            for (ITYPE k = 0; k < K; k++) {
                C[i * K + k] += A[i * N + j] * B[j * K + k];
            }
        }
    }
}

void fill_matrix(T *M, ITYPE nrows, ITYPE ncols)
{
    for (ITYPE i = 0; i < (nrows * ncols); i++) {
        M[i] = RAND_MAX / rand() * RAND_MAX;
    }
}

int main(int argc, char *argv[])
{

    ITYPE M = 1024;
    ITYPE N = 1024;
    ITYPE K = 128;

	printf("MM debug print 0\n");
    long opt;
    while ((opt = getopt(argc, argv, "M:N:K:t:")) != -1) {
        switch (opt)
        {
        case 'M':
            M = atol(optarg);
			printf("MMMMMMMMMMMMMMMMMMMMMMM = %ld\n",M);
            break;
        case 'N':
            N = atol(optarg);
            break;

        case 'K':
            K = atol(optarg);
            break;

        case 't':
            NUM_THREADS = atol(optarg);
            break;

        default:
            break;
        }
    }
   
	printf("MM debug print 1\n");

	return 0;
    omp_set_num_threads(NUM_THREADS);

    T* A = (T*)malloc(sizeof(T) * M * N);
    T* B = (T*)malloc(sizeof(T) * N * K);
    T* C = (T*)malloc(sizeof(T) * M * K);

	printf("MM debug print 1\n");
    #ifdef DEBUG
        T* D = malloc(sizeof(T) * M * K);
        assert(D != NULL && "Could not allocate debug matrix");
        memset(D, 0, sizeof(T) * M * K);
    #endif

    if (!A || !B || !C) {
        fprintf(stderr, "Could not allocate memory\n");
        exit(EXIT_FAILURE);
    }

	printf("MM debug print 2\n");


    fill_matrix(A, M, N);
    fill_matrix(B, N, K);
    memset(C, 0, sizeof(T) * N * K);
	printf("MM debug print 3\n");
    
    tiled_mm(A, B, C, M, N, K);
    
    #ifdef DEBUG
        simple_mm(A, B, D, M, N, K);
        if (memcmp(C, D, sizeof(T) * M * K)) {
            fprintf(stderr, "Something went wrong in the tiled multiplication\n");
        } else {
            fprintf(stderr, "All good here!\n");
        }
    #endif

    return 0;
}

