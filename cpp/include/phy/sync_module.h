#ifndef WIFI80211A_SYNC_MODULE_H
#define WIFI80211A_SYNC_MODULE_H

#include <complex>
#include <cstdint>
#include <vector>

#include "phy/helpers.h"
#include "phy/link_settings.h"

namespace wifi80211a {

    struct SyncResult {
        int packet_start;
        double cfo_hz;
        complexVector H;
    };

    SyncResult detect_and_sync(const std::vector<std::complex<double>>& signal, const LinkSettings& linkSettings);
    SyncResult coarse_sync(const std::vector<std::complex<double>>& signal, const LinkSettings& linkSettings);
    SyncResult fine_sync(const std::vector<std::complex<double>>& signal, const SyncResult& coarse_result, const LinkSettings& linkSettings);
    std::vector<std::complex<double>> channel_estimation(const std::vector<std::complex<double>>& signal, const SyncResult& fine_result, const LinkSettings& linkSettings);
    int longest_plateau_end(const std::vector<int>& above, int min_plateau);
}
#endif //WIFI80211A_SYNC_MODULE_H
