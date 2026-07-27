#pragma once

#include <cstdint>
#include <map>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace wifi80211a {

    enum class ModulationTypes {
        BPSK,
        QPSK,
        QAM16,
        QAM64
    };

    enum class CodingRates {
        R12,
        R23,
        R34
    };

    inline const std::unordered_map<int, std::tuple<ModulationTypes, CodingRates>> rateMap = {
        {1, {ModulationTypes::BPSK, CodingRates::R12}},
        {2, {ModulationTypes::BPSK, CodingRates::R34}},
        {3, {ModulationTypes::QPSK, CodingRates::R12}},
        {4, {ModulationTypes::QPSK, CodingRates::R34}},
        {5, {ModulationTypes::QAM16, CodingRates::R12}},
        {6, {ModulationTypes::QAM16, CodingRates::R34}},
        {7, {ModulationTypes::QAM64, CodingRates::R23}},
        {8, {ModulationTypes::QAM64, CodingRates::R34}}
    };

    inline const std::map<std::tuple<ModulationTypes, CodingRates>, int> inverseRateMap = {
        {{ModulationTypes::BPSK, CodingRates::R12}, 1},
        {{ModulationTypes::BPSK, CodingRates::R34}, 2},
        {{ModulationTypes::QPSK, CodingRates::R12}, 3},
        {{ModulationTypes::QPSK, CodingRates::R34}, 4},
        {{ModulationTypes::QAM16, CodingRates::R12}, 5},
        {{ModulationTypes::QAM16, CodingRates::R34}, 6},
        {{ModulationTypes::QAM64, CodingRates::R23}, 7},
        {{ModulationTypes::QAM64, CodingRates::R34}, 8}
    };

    // IEEE 802.11a-1999 17.3.4.1 (Table 78): the SIGNAL field's 4-bit RATE
    // codeword for each rate is a fixed, non-sequential code -- NOT a binary
    // index of `rateMap`'s keys. Packed LSB-first (bit0=R1 ... bit3=R4) to
    // match int_to_bits()/decode_signal_header()'s bit order. E.g. 36 Mb/s
    // (16-QAM, R=3/4) transmits R1..R4 = 1,0,1,1 per Annex G Table G.7.
    inline const std::unordered_map<int, int> rateCodeword = {
        {1, 0b1011}, // 6 Mb/s:  R1..R4 = 1,1,0,1
        {2, 0b1111}, // 9 Mb/s:  R1..R4 = 1,1,1,1
        {3, 0b1010}, // 12 Mb/s: R1..R4 = 0,1,0,1
        {4, 0b1110}, // 18 Mb/s: R1..R4 = 0,1,1,1
        {5, 0b1001}, // 24 Mb/s: R1..R4 = 1,0,0,1
        {6, 0b1101}, // 36 Mb/s: R1..R4 = 1,0,1,1
        {7, 0b1000}, // 48 Mb/s: R1..R4 = 0,0,0,1
        {8, 0b1100}, // 54 Mb/s: R1..R4 = 0,0,1,1
    };

    inline const std::unordered_map<int, int> rateCodewordToValue = {
        {0b1011, 1}, {0b1111, 2}, {0b1010, 3}, {0b1110, 4},
        {0b1001, 5}, {0b1101, 6}, {0b1000, 7}, {0b1100, 8},
    };

    class LinkSettings {
    public:
        LinkSettings() = default;
        LinkSettings(ModulationTypes modulation_type_arg, CodingRates coding_rate_arg);

        CodingRates getCodingRate() const;
        ModulationTypes getModulationType() const;
        int getErrorCorrectingCode() const;
        int getNumSubcarriers() const;
        double getOFDMSymbolDuration() const;
        double getGuardInterval() const;
        double getOccupiedBandwidth() const;
        int getNumberOfPilots() const;
        int getCPLenData() const;
        int getCPLenTraining() const;
        int getNFFT() const;
        int getNCPBS() const;
        double getT() const;
        std::vector<int> getPilotPositions() const;

        bool changeCodingRate(CodingRates new_coding_rate);
        bool changeModulationType(ModulationTypes new_modulation_type);

    private:
        ModulationTypes modulation_type = ModulationTypes::BPSK;
        CodingRates coding_rate = CodingRates::R12;

        int error_correcting_code = 7;
        int num_subcarriers = 52;
        int num_pilots = 4;
        int nFFT = 64;
        int cp_len_data = 16;
        int cp_len_training = 32;
        double ofdm_symbol_duration = 4e-6;   // 4 µs
        double T = 4e-6 - 0.8e-6;
        double guard_interval = 0.8e-6;      // 0.8 µs
        double occupied_bandwidth = 16.6e6;  // 16.6 MHz


        std::map<ModulationTypes, int8_t> bits_per_subcarrier = {
            {ModulationTypes::BPSK, 1},
            {ModulationTypes::QPSK, 2},
            {ModulationTypes::QAM16, 4},
            {ModulationTypes::QAM64, 6}
        };

        std::vector<int> pilot_positions = {-21, -7, 7, 21};
    };
}
