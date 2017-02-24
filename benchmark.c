#include <time.h>
#include <math.h>
#include <stdint.h>

typedef struct benchmark {
	const char* name;
	uint64_t cpu_time_spent;
	uint32_t runs;
	char byte_alignment_padding[4];
	double average_seconds;
} benchmark;

benchmark run_benchmark( const char* name, void (*function)(void*), void *params );

benchmark run_benchmark( const char* name, void (*function)(void*), void *params ) {
	
	benchmark b = { .name = name, .cpu_time_spent = 0, .runs = 0 };
	
	uint64_t cpu_time_start = 0;
	uint64_t cpu_time = 0;

	double x;
	double mean = 0;
	double m2 = 0;
	double variance = 0;
	double delta = 0;
	double cv = 0, cv_diff = 100; // coefficient of variation (sd/mean)
	double sd = 0;

	// run once, which we need to do always, but also avoids a variance calc fail or an if inside the loop
	b.runs++;

	cpu_time_start = clock();
	function( params );
	mean = clock() - cpu_time_start;

	// keep running until the CV stabilizes
	while( cv_diff > 0.0001 ) {
		b.runs++;

		cpu_time_start = clock();
		function( params );
		cpu_time = clock() - cpu_time_start;
		b.cpu_time_spent += cpu_time;

		// every run contributes to the average the difference from the average, in proportion
		// to the share of total runs it is.
		// for the variance: welford's method
		x = (double) cpu_time;
		delta = x - mean;
		mean += delta/(double)b.runs;
		m2 += delta * (x - mean);
		
		variance = m2 / (double)b.runs;
		sd = sqrt(variance);
		cv_diff = fabs(cv - sd/mean);
		cv = sd/mean;
		// printf("N: %llu Time %.0f\tMean: %.10f\t Var: %.10f\t SD: %.10f\t CV: %.10f\t CVD: %.5f\n", b.runs, x, mean, variance, sd, cv, cv_diff);
	}
	
	b.average_seconds = ((double)b.cpu_time_spent / (double)CLOCKS_PER_SEC) / (double)b.runs;

	return b;
}

// clock_t cpu_time_start = clock();
// gc.cpu_time_used = ((double)( clock() - cpu_time_start ) / CLOCKS_PER_SEC );


