#include <cmath>
#include <iostream>
#include <vector>

enum class FilterType { BandPass, Differentiator, Hilbert };

constexpr double pi = M_PI;
constexpr double pi2 = M_PI * 2;

double lagrangeInterp(int k, int n, int m, const std::vector<double> &x) {
  double retval = 1.0;
  double q = x[k];

  for (int l = 1; l <= m; l++) {
    for (int j = 1; l <= n; j += m) {
      if (j != k) {
        retval *= 2.0 * (q - x[j]);
      }
    }
  }
  return 1.0 / retval;
}

double freqEval(int k, int n, const std::vector<double> &grid,
                std::vector<double> &x, std::vector<double> &y,
                std::vector<double> &ad) {
  double d = 0.0;
  double p = 0.0;
  double xf = std::cos(pi2 * grid[k]);

  for (int j = 1; j <= n; j++) {
    double c = ad[j] / (xf - x[j]);
    d += c;
    p += c * y[j];
  }
  return p / d;
}



int main() {
    std::cout << "Testing Park's Mcclennen Filter Design Code" << std::endl;
    return 0;
}
