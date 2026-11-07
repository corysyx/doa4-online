// Positive control for suitconv_fp_scan.py and the contraction probe: built WITH FMA and contraction
// (/clang:-march=x86-64-v3 /clang:-mfma /clang:-ffp-contract=fast) and calling std::pow and std::hypot(float), so
// the scan must report "fma >= 1" and libm pow and _hypotf for its object, and, run as a program on a CPU with FMA,
// it prints the fused value of the
// contraction probe (-2^-60), which the converter's ContractionProbe must not give. Not part of any build target.

#include <cmath>
#include <cstdio>
#include <cstdlib>

double ControlFused(double a, double b, double c) { return a * b + c; }

double ControlPow(double x, double y) { return std::pow(x, y); }

// The C runtime's underscore alias: std::hypot(float, float) compiles to a call of _hypotf, which the scan must
// report as well.
float ControlHypotf(float x, float y) { return std::hypot(x, y); }

int main(int argc, char** argv) {
  // Inputs from the command line where given, so nothing folds at compile time.
  const double a = argc > 1 ? std::atof(argv[1]) : 1.0 + 0x1p-30;
  const double b = argc > 2 ? std::atof(argv[2]) : 1.0 - 0x1p-30;
  const double c = argc > 3 ? std::atof(argv[3]) : -1.0;
  const double fused = ControlFused(a, b, c);
  std::printf("control contraction probe: %a (fused: %s)\n", fused, fused == -0x1p-60 ? "yes" : "no");
  std::printf("control pow(2, 0.5): %a\n", ControlPow(2.0, argc > 4 ? std::atof(argv[4]) : 0.5));
  std::printf("control hypotf(3, 4): %a\n", double(ControlHypotf(3.0f, argc > 5 ? float(std::atof(argv[5])) : 4.0f)));
  return 0;
}
