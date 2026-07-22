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

    SyncResult detect_and_sync(const complexVector& signal, const LinkSettings& linkSettings);
    SyncResult coarse_sync(const complexVector& signal,
        const LinkSettings& linkSettings, const double& threshold = 0.7);
    SyncResult coarse_cfo(const complexVector& signal, SyncResult& syncResult, const LinkSettings& linkSettings);
    SyncResult fine_sync(const complexVector& signal, const SyncResult& coarse_result, const LinkSettings& linkSettings);
    complexVector channel_estimation(const complexVector& signal, const SyncResult& fine_result, const LinkSettings& linkSettings);
    /// Least-squares channel estimate obtained by deconvolving the LTF cyclic
    /// prefix via a shift-matrix pseudo-inverse. Falls back to `channel_estimation`
    /// if the shift matrix is (numerically) singular.
    complexVector ls_channel_estimation(const complexVector& signal, const SyncResult& sync, const LinkSettings& linkSettings);
    int longest_plateau_end(const std::vector<int>& above, int min_plateau);
}
#endif //WIFI80211A_SYNC_MODULE_H
