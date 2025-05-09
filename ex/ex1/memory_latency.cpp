// OS 2025 EX1

#include "memory_latency.h"
#include "measure.h"
#include <cmath>
#include <iostream>

#define GALOIS_POLYNOMIAL ((1ULL << 63) | (1ULL << 62) | (1ULL << 60) | (1ULL << 59))

#define MIN_SIZE 100;
/**
 * Converts the struct timespec to time in nano-seconds.
 * @param t - the struct timespec to convert.
 * @return - the value of time in nano-seconds.
 */
uint64_t nanosectime(struct timespec t)
{
	return (uint64_t )t.tv_sec * pow(10,9) + (uint64_t )t.tv_nsec;
}

/**
* Measures the average latency of accessing a given array in a sequential order.
* @param repeat - the number of times to repeat the measurement for and average on.
* @param arr - an allocated (not empty) array to preform measurement on.
* @param arr_size - the length of the array arr.
* @param zero - a variable containing zero in a way that the compiler doesn't "know" it in compilation time.
* @return struct measurement containing the measurement with the following fields:
*      double baseline - the average time (ns) taken to preform the measured operation without memory access.
*      double access_time - the average time (ns) taken to preform the measured operation with memory access.
*      uint64_t rnd - the variable used to randomly access the array, returned to prevent compiler optimizations.
*/
struct measurement measure_sequential_latency(uint64_t repeat, array_element_t* arr, uint64_t arr_size, uint64_t zero)
{
    repeat = arr_size > repeat ? arr_size:repeat; // Make sure repeat >= arr_size

    // Baseline measurement:
    struct timespec t0;
    timespec_get(&t0, TIME_UTC);
    register uint64_t rnd=12345;
    for (register uint64_t i = 0; i < repeat; i++)
    {
        register uint64_t index = i % arr_size;
        rnd ^= (index & zero) ^ (index % rnd);
        rnd = (rnd >> 1) ^ ((0-(rnd & 1)) & GALOIS_POLYNOMIAL);  // Advance rnd pseudo-randomly (using Galois LFSR)
    }
    struct timespec t1;
    timespec_get(&t1, TIME_UTC);

    // Memory access measurement:
    struct timespec t2;
    timespec_get(&t2, TIME_UTC);
    rnd=(rnd & zero) ^ 12345;
    for (register uint64_t i = 0; i < repeat; i++)
    {
        register uint64_t index = i % arr_size;
        rnd ^= (arr[index] & zero)^ (index % rnd);
        rnd = (rnd >> 1) ^ ((0-(rnd & 1)) & GALOIS_POLYNOMIAL);  // Advance rnd pseudo-randomly (using Galois LFSR)
    }
    struct timespec t3;
    timespec_get(&t3, TIME_UTC);

    // Calculate baseline and memory access times:
    double baseline_per_cycle=(double)(nanosectime(t1)- nanosectime(t0))/(repeat);
    double memory_per_cycle=(double)(nanosectime(t3)- nanosectime(t2))/(repeat);
    struct measurement result;

    result.baseline = baseline_per_cycle;
    result.access_time = memory_per_cycle;
    result.rnd = rnd;
    return result;
}

/**
 * Runs the logic of the memory_latency program. Measures the access latency for random and sequential memory access
 * patterns.
 * Usage: './memory_latency max_size factor repeat' where:
 *      - max_size - the maximum size in bytes of the array to measure access latency for.
 *      - factor - the factor in the geometric series representing the array sizes to check.
 *      - repeat - the number of times each measurement should be repeated for and averaged on.
 * The program will print output to stdout in the following format:
 *      mem_size_1,offset_1,offset_sequential_1
 *      mem_size_2,offset_2,offset_sequential_2
 *              ...
 *              ...
 *              ...
 */
int main(int argc, char* argv[])
{
    // zero==0, but the compiler doesn't know it. Use as the zero arg of measure_latency and measure_sequential_latency.
    struct timespec t_dummy;
    timespec_get(&t_dummy, TIME_UTC);
    const uint64_t zero = nanosectime(t_dummy)>1000000000ull?0:nanosectime(t_dummy);

    // Your code here

    try {
        if (argc != 4) {
            std::cerr << "Usage: " << argv[0] << " max_size factor repeat\n";
            return EXIT_FAILURE;
        }

        if (argv[3][0] == '-' || argv[3][0] == '0') {
            throw std::invalid_argument("repeat must be > 0");
        }

        uint64_t max_size = std::stoull(argv[1]);
        float factor = std::stof(argv[2]);
        uint64_t repeat = std::stoull(argv[3]);

        if (factor <= 1.0f) {
            throw std::invalid_argument("factor must be > 1.0");
        }
        if (max_size <100){
            throw std::invalid_argument("max size must be bigger than 100");
        }

        uint64_t i=MIN_SIZE;
        while (i<max_size){

            uint64_t * arr = (uint64_t *) malloc(i);

            for (uint64_t j=0; j<(i/ sizeof(uint64_t) ); j++)
            {
                arr[j] = j+1;
            }


            struct measurement sequential_result =
                    measure_sequential_latency(repeat, arr, i/ (sizeof(uint64_t )), zero);
            struct measurement random_result =
                    measure_latency(repeat, arr, i/(sizeof (uint64_t)), zero);

            std::cout << i << "," << random_result.access_time - random_result.baseline
                      << "," << sequential_result.access_time - sequential_result.baseline << std::endl;

            free(arr);
            i = (uint64_t) ceil(i * factor);
        }


    } catch (const std::exception& e) {
        std::cerr << "Argument error: " << e.what() << std::endl;
        exit(-1);
    }





   }

