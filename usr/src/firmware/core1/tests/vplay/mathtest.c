/* mathtest -- compat/amix-math.c against known answers, and its speed */
#include <stdio.h>
#include <sys/time.h>

double amix_rint(double), amix_trunc(double), amix_floor(double), amix_ceil(double);
double amix_round(double), amix_sqrt(double);
long amix_lrint(double), amix_lrintf(float);

static int bad;

static void eq(const char *what, double x, double got, double want)
{
	if (got != want) {
		printf("FAIL %s(%g) = %.17g, want %.17g\n", what, x, got, want);
		bad++;
	}
}

int main(void)
{
	static const double in[] = { 2.5, 3.5, -2.5, -1.5, 1.2, -1.2, -2.0, 0.0, 32767.6, -0.5, 1e15 + 0.5 };
	static const double rint_w[] = { 2, 4, -2, -2, 1, -1, -2, 0, 32768, -0.0, 1e15 };
	static const double floor_w[] = { 2, 3, -3, -2, 1, -2, -2, 0, 32767, -1, 1e15 };
	static const double ceil_w[] = { 3, 4, -2, -1, 2, -1, -2, 0, 32768, -0.0, 1e15 + 1 };
	static const double trunc_w[] = { 2, 3, -2, -1, 1, -1, -2, 0, 32767, -0.0, 1e15 };
	static const double round_w[] = { 3, 4, -3, -2, 1, -1, -2, 0, 32768, -1, 1e15 + 1 };
	struct timeval t0, t1;
	volatile long sink = 0;
	unsigned i;
	long n;

	for (i = 0; i < sizeof in / sizeof in[0]; i++) {
		eq("rint", in[i], amix_rint(in[i]), rint_w[i]);
		eq("floor", in[i], amix_floor(in[i]), floor_w[i]);
		eq("ceil", in[i], amix_ceil(in[i]), ceil_w[i]);
		eq("trunc", in[i], amix_trunc(in[i]), trunc_w[i]);
		eq("round", in[i], amix_round(in[i]), round_w[i]);
		if (in[i] < 1e9)
			eq("lrint", in[i], (double)amix_lrint(in[i]), rint_w[i]);
	}
	eq("lrintf", 2.5, (double)amix_lrintf(2.5f), 2);
	eq("lrintf", -32768.4, (double)amix_lrintf(-32768.4f), -32768);
	eq("sqrt", 2, amix_sqrt(2.0), 1.4142135623730951);
	eq("sqrt", 1e10, amix_sqrt(1e10), 1e5);
	gettimeofday(&t0, NULL);
	for (n = 0; n < 1000000; n++)
		sink += amix_lrintf((float)n * 0.37f);
	gettimeofday(&t1, NULL);
	printf("lrintf: %.3f us per call\n", ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_usec - t0.tv_usec)) / 1e6);
	printf("%s (%d failed)\n", bad ? "FAILED" : "all correct", bad);
	return bad != 0;
}
