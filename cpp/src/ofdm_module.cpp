#include <vector>
#include <complex>
#include <cstring>
#include "phy/ofdm_module.h"
#include <fftw3.h>
#include <set>

#include "phy/helpers.h"


namespace wifi80211a
{

    complexVector OFDMModule::modulate(const LinkSettings& link_settings,
                                       const complexVector& data, PilotLFSR pilot_lfsr)
    {
        const int nFFT = link_settings.getNFFT();
        const int cp_len = link_settings.getCPLenData();

        // 48 data subcarriers = 52 active − 4 pilots
        const int K = link_settings.getNumSubcarriers() - link_settings.getNumberOfPilots();
        const int num_ofdm_blocks = static_cast<int>(std::ceil(static_cast<double>(data.size()) / K));

        const std::vector<int> pilot_positions = link_settings.getPilotPositions();
        const std::set<int> pilot_set(pilot_positions.begin(), pilot_positions.end());

        std::vector<fftw_complex> in(nFFT), out(nFFT);
        complexVector output_ofdm_waveform;
        fftw_plan plan = fftw_plan_dft_1d(nFFT, in.data(), out.data(), FFTW_BACKWARD, FFTW_ESTIMATE);

        for (int i = 0; i < num_ofdm_blocks; i++)
        {
            const int block_start = i * K;
            memset(in.data(), 0, nFFT * sizeof(fftw_complex));

            // Insert pilots: one PRBS bit per symbol, weighted by per-subcarrier polarity
            const double prbs = pilot_lfsr.next_polarity();
            for (int p = 0; p < 4; p++)
            {
                int k = pilot_positions[p];
                int fft_bin = fft_bin_from_subcarrier(k, nFFT);
                in[fft_bin][0] = prbs * PilotLFSR::POLARITY[p];
                in[fft_bin][1] = 0.0;
            }

            // Insert data subcarriers in order from −26 to +26, skipping DC and pilots
            int data_idx = 0;
            for (int k = -26; k <= 26; k++)
            {
                if (k == 0) continue;
                if (pilot_set.contains(k)) continue;

                int fft_bin = fft_bin_from_subcarrier(k, nFFT);
                int src = block_start + data_idx;
                if (src < static_cast<int>(data.size()))
                {
                    in[fft_bin][0] = data[src].real();
                    in[fft_bin][1] = data[src].imag();
                }
                data_idx++;
            }

            fftw_execute(plan);
            for (int j = 0; j < nFFT; j++)
            {
                out[j][0] /= nFFT;
                out[j][1] /= nFFT;
            }

            // Cyclic prefix
            for (int j = nFFT - cp_len; j < nFFT; j++)
            {
                output_ofdm_waveform.emplace_back(out[j][0], out[j][1]);
            }
            // IFFT output
            for (int j = 0; j < nFFT; j++)
            {
                output_ofdm_waveform.emplace_back(out[j][0], out[j][1]);
            }
        }

        fftw_destroy_plan(plan);
        return output_ofdm_waveform;
    }


    OFDMDemodResult OFDMModule::demodulate(const LinkSettings& linkSettings, const complexVector& data)
    {
        const int nFFT = linkSettings.getNFFT();
        const int cp_len = linkSettings.getCPLenData();

        const int ofdm_block_size = cp_len + nFFT;
        const int num_ofdm_blocks = static_cast<int>(data.size()) / ofdm_block_size;

        const std::vector<int> pilot_positions = linkSettings.getPilotPositions();

        std::vector<fftw_complex> in(nFFT), out(nFFT);
        OFDMDemodResult result;
        result.freq_bins.reserve(static_cast<std::size_t>(num_ofdm_blocks) * static_cast<std::size_t>(nFFT));
        result.pilots.reserve(static_cast<std::size_t>(num_ofdm_blocks) * 4u);

        fftw_plan plan = fftw_plan_dft_1d(nFFT, in.data(), out.data(), FFTW_FORWARD, FFTW_ESTIMATE);

        for (int i = 0; i < num_ofdm_blocks; i++)
        {
            const int sym_start = i * ofdm_block_size;
            const int body_start = sym_start + cp_len;
            for (int k = 0; k < nFFT; k++)
            {
                const int idx = body_start + k;
                in[k][0] = data[idx].real();
                in[k][1] = data[idx].imag();
            }
            fftw_execute(plan);
            for (int j = 0; j < nFFT; j++)
            {
                result.freq_bins.emplace_back(out[j][0], out[j][1]);
            }
            for (int p = 0; p < 4; ++p)
            {
                const int b = fft_bin_from_subcarrier(pilot_positions[static_cast<std::size_t>(p)], nFFT);
                result.pilots.emplace_back(out[b][0], out[b][1]);
            }
        }
        fftw_destroy_plan(plan);
        return result;
    }
}
