#pragma once

#include <cstdint>
#include <map>
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
