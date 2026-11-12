/*
 * Counts the software double-precision operations the physics executes, in the
 * PSP build. Built only with `make DFCOUNT=1`, which adds -Wl,--wrap=<helper>
 * for each symbol below so every call the physics makes lands here first.
 *
 * Why this exists: this toolchain has no double-precision FPU path for the PSP,
 * so a `double` multiply compiles to a call into libgcc (__muldf3, tens of
 * integer instructions) instead of one instruction, and the physics does its
 * maths in double and narrows to float. That call traffic - not the
 * single-precision work, which is hardware - is what a PSP actually pays for,
 * and it is the one cost term that can be counted exactly instead of estimated.
 * See M2 in PROGRESS.md.
 *
 * The wrappers add one increment per call, so this build must not be used for
 * timing, only for counting. bench_phys.c snapshots the counters either side of
 * its measurement loop, so the bench's own printf - which formats doubles and is
 * heavy in the same helpers - is not counted.
 */
unsigned long dfc_mul, dfc_add, dfc_sub, dfc_div;
unsigned long dfc_cmp, dfc_trunc, dfc_extend, dfc_f2d, dfc_d2i, dfc_cosf;

double __real___muldf3(double, double);
double __real___adddf3(double, double);
double __real___subdf3(double, double);
double __real___divdf3(double, double);
int __real___ltdf2(double, double);
int __real___ledf2(double, double);
int __real___gtdf2(double, double);
int __real___gedf2(double, double);
int __real___eqdf2(double, double);
int __real___unorddf2(double, double);
float __real___truncdfsf2(double);
double __real___extendsfdf2(float);
double __real___floatsidf(int);
int __real___fixdfsi(double);
float __real_cosf(float);

double __wrap___muldf3(double a, double b) { dfc_mul++;   return __real___muldf3(a, b); }
double __wrap___adddf3(double a, double b) { dfc_add++;   return __real___adddf3(a, b); }
double __wrap___subdf3(double a, double b) { dfc_sub++;   return __real___subdf3(a, b); }
double __wrap___divdf3(double a, double b) { dfc_div++;   return __real___divdf3(a, b); }

int __wrap___ltdf2(double a, double b)     { dfc_cmp++;   return __real___ltdf2(a, b); }
int __wrap___ledf2(double a, double b)     { dfc_cmp++;   return __real___ledf2(a, b); }
int __wrap___gtdf2(double a, double b)     { dfc_cmp++;   return __real___gtdf2(a, b); }
int __wrap___gedf2(double a, double b)     { dfc_cmp++;   return __real___gedf2(a, b); }
int __wrap___eqdf2(double a, double b)     { dfc_cmp++;   return __real___eqdf2(a, b); }
int __wrap___unorddf2(double a, double b)  { dfc_cmp++;   return __real___unorddf2(a, b); }

float __wrap___truncdfsf2(double a)  { dfc_trunc++;  return __real___truncdfsf2(a); }
double __wrap___extendsfdf2(float a) { dfc_extend++; return __real___extendsfdf2(a); }
double __wrap___floatsidf(int a)     { dfc_f2d++;    return __real___floatsidf(a); }
int __wrap___fixdfsi(double a)       { dfc_d2i++;    return __real___fixdfsi(a); }
float __wrap_cosf(float a)           { dfc_cosf++;   return __real_cosf(a); }

/* Snapshot for the caller: [0]=mul [1]=add [2]=sub [3]=div [4]=cmp [5]=df->sf
 * [6]=sf->df [7]=int->double [8]=double->int [9]=cosf [10]=total. */
void dfc_snapshot(unsigned long out[11])
{
	out[0] = dfc_mul;
	out[1] = dfc_add;
	out[2] = dfc_sub;
	out[3] = dfc_div;
	out[4] = dfc_cmp;
	out[5] = dfc_trunc;
	out[6] = dfc_extend;
	out[7] = dfc_f2d;
	out[8] = dfc_d2i;
	out[9] = dfc_cosf;
	out[10] = out[0] + out[1] + out[2] + out[3] + out[4] + out[5] +
	         out[6] + out[7] + out[8] + out[9];
}
