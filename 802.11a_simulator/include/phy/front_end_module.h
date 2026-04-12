//
// Created by Kanishka on 4/8/2026.
//

#ifndef WIFI80211A_FRONT_END_MODULE_H
#define WIFI80211A_FRONT_END_MODULE_H
#include <complex>
#include <vector>

#include "link_settings.h"

namespace wifi80211a
{

    class FrontEndModule {
    public:
        FrontEndModule() = default;
        std::pmr::vector<std::complex<double>> pulse_shape(const std::pmr::vector<std::complex<double>> &signal, const double& T);
        std::vector<double> iq_modulate(const std::pmr::vector<std::complex<double>> &signal, const double& T) const;

    private:
        std::vector<double> generate_raised_cosine_(const double& T);
        static double sinc_(double x);
        static int next_pow_of_2_(unsigned int x);

        const int sps_ = 4; // Samples per symbol
        const double beta_ = 0.25; // raised cosine beta
        const int span_ = 5; // each raised cosine spans 10 symbols
    };
}



#endif //WIFI80211A_FRONT_END_MODULE_H
