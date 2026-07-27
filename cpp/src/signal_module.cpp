//
// Created by Kanishka Roy on 7/23/26.
//
// C++ port of python/signal_module.py

#include "../include/phy/signal_module.h"

#include <complex>
#include <set>
#include <stdexcept>

#include "phy/fec_module.h"
#include "phy/interleaver_module.h"
#include "phy/modulation_module.h"
#include "phy/ofdm_module.h"
#include "phy/pilot_utils.h"

namespace wifi80211a
{
    namespace
    {
        // SIGNAL is always one BPSK symbol: NCBPS=48, NBPSC=1.
        constexpr int kSignalNCBPS = 48;
        constexpr int kSignalNBPSC = 1;

        int even_parity_(const std::vector<int>& bits, int count)
        {
            int parity = 0;
            for (int i = 0; i < count; i++)
            {
                parity ^= bits[i];
            }
            return parity;
        }
    }

    complexVector create_signal_header(const LinkSettings& linkSettings, int& psduLengthOctets)
    {
        std::vector<int> signalBits;

        const int rateValue = inverseRateMap.at({linkSettings.getModulationType(), linkSettings.getCodingRate()});
        const std::vector<int> rateBits = int_to_bits(static_cast<uint64_t>(rateCodeword.at(rateValue)), 4);
        signalBits.insert(signalBits.end(), rateBits.begin(), rateBits.end());

        signalBits.push_back(0); // reserved

        const std::vector<int> lengthBits = int_to_bits(static_cast<uint64_t>(psduLengthOctets), 12);
        signalBits.insert(signalBits.end(), lengthBits.begin(), lengthBits.end());

        signalBits.push_back(even_parity_(signalBits, 17)); // even parity over bits 0-16

        // append_convolutional_tail_ supplies the 6 zero SIGNAL TAIL bits (18-23),
        const std::vector<int> tailedBits = append_convolutional_tail_(signalBits);
        const std::vector<int> encodedBits = convolutional_encoder_mother_(tailedBits);

        const std::vector<int> interleavedBits = interleave(encodedBits, kSignalNCBPS, kSignalNBPSC);

        const complexVector modulatedSymbols = map_bits_to_constellation(
            interleavedBits, ModulationTypes::BPSK, kSignalNBPSC);

        // OFDM modulate (pilots + IFFT + cyclic prefix) -> one 80-sample symbol
        PilotLFSR pilotLfsr;
        return modulate(linkSettings, modulatedSymbols, pilotLfsr);
    }

    SignalOutput decode_signal_header(LinkSettings& linkSettings, complexVector& signalHeader, complexVector& H)
    {
        const OFDMDemodResult demod = demodulate(linkSettings, signalHeader);

        const int nFFT = linkSettings.getNFFT();
        const std::vector<int> pilotPositions = linkSettings.getPilotPositions();
        const std::set<int> pilotSet(pilotPositions.begin(), pilotPositions.end());

        // Data-subcarrier bins in the same order create_signal_header assigned them
        complexVector dataBins;
        dataBins.reserve(kSignalNCBPS);
        for (int k = -26; k <= 26; k++)
        {
            if (k == 0 || pilotSet.contains(k)) continue;

            const int fftBin = fft_bin_from_subcarrier(k, nFFT);
            std::complex<double> sample = demod.freq_bins[fftBin];
            if (!H.empty())
            {
                const std::complex<double> H_k =
                    (std::abs(H[fftBin]) > 1e-10) ? H[fftBin] : std::complex<double>(1.0, 0.0);
                sample /= H_k;
            }
            dataBins.push_back(sample);
        }

        const std::vector<int> codedBits = map_constellation_to_bits(dataBins, ModulationTypes::BPSK, kSignalNBPSC);
        const std::vector<int> deinterleavedBits = deinterleave(codedBits, kSignalNCBPS, kSignalNBPSC);

        const std::vector<int> maskBits(deinterleavedBits.size(), 1);
        std::vector<int> decodedBits = viterbi_decode_(deinterleavedBits, maskBits);
        decodedBits.resize(18); // drop the 6 SIGNAL TAIL bits

        const int rateCodewordBits = decodedBits[0] | (decodedBits[1] << 1) | (decodedBits[2] << 2) | (decodedBits[3] << 3);
        const int parityBit = decodedBits[17];

        const int expectedParity = even_parity_(decodedBits, 17);
        if (expectedParity != parityBit)
        {
            throw std::runtime_error("SIGNAL field parity check failed");
        }

        const auto [modulationType, codingRate] = rateMap.at(rateCodewordToValue.at(rateCodewordBits));

        int psduLengthOctets = 0;
        for (int i = 0; i < 12; i++)
        {
            psduLengthOctets |= decodedBits[5 + i] << i;
        }

        linkSettings.changeModulationType(modulationType);
        linkSettings.changeCodingRate(codingRate);

        return SignalOutput{modulationType, codingRate, psduLengthOctets};
    }
}
