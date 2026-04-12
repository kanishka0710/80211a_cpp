#include <vector>
#include <complex>
#include <array>
#include <cstring>
#include "phy/ofdm_module.h"
#include <fftw3.h>
#include <set>


namespace wifi80211a
{
    // Fixed polarity weights per pilot subcarrier: [-21, -7, +7, +21] → [+1, +1, +1, -1]
    static constexpr std::array<int, 4> PILOT_POLARITY = {+1, +1, +1, -1};

    double OFDMModulator::next_pilot_polarity()
    {
        // LFSR taps at positions 7 and 4 (1-indexed), i.e. bits 6 and 3 in a 7-bit register
        int out_bit = (pilot_lfsr >> 6) & 1;
        int feedback = out_bit ^ ((pilot_lfsr >> 3) & 1);
        pilot_lfsr = static_cast<uint8_t>(((pilot_lfsr << 1) | feedback) & 0x7F);
        return out_bit ? -1.0 : +1.0; // BPSK: 0 → +1, 1 → −1
    }

    std::pmr::vector<std::complex<double>> OFDMModulator::modulate(LinkSettings link_settings,
                                                                   std::pmr::vector<std::complex<double>> data)
    {
        const int nFFT = link_settings.getNFFT();
        const int cp_len = link_settings.getCPLenData();

        // 48 data subcarriers = 52 active − 4 pilots
        const int K = link_settings.getNumSubcarriers() - link_settings.getNumberOfPilots();
        const int num_ofdm_blocks = static_cast<int>(std::ceil((double)data.size() / K));

        const std::vector<int> pilot_positions = link_settings.getPilotPositions();
        const std::set<int> pilot_set(pilot_positions.begin(), pilot_positions.end());

        std::vector<fftw_complex> in(nFFT), out(nFFT);
        std::pmr::vector<std::complex<double>> output_ofdm_waveform;
        fftw_plan plan = fftw_plan_dft_1d(nFFT, in.data(), out.data(), FFTW_BACKWARD, FFTW_ESTIMATE);

        for (int i = 0; i < num_ofdm_blocks; i++)
        {
            const int block_start = i * K;
            memset(in.data(), 0, nFFT * sizeof(fftw_complex));

            // Insert pilots: one PRBS bit per symbol, weighted by per-subcarrier polarity
            const double prbs = next_pilot_polarity();
            for (int p = 0; p < 4; p++)
            {
                int k = pilot_positions[p];
                int fft_bin = (k > 0) ? k : nFFT + k; // negative k wraps to upper bins
                in[fft_bin][0] = prbs * PILOT_POLARITY[p];
                in[fft_bin][1] = 0.0;
            }

            // Insert data subcarriers in order from −26 to +26, skipping DC and pilots
            int data_idx = 0;
            for (int k = -26; k <= 26; k++)
            {
                if (k == 0) continue;
                if (pilot_set.contains(k)) continue;

                int fft_bin = (k > 0) ? k : nFFT + k;
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
}
