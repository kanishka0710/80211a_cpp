#include "phy/link_settings.h"

#include <map>

namespace wifi80211a {

LinkSettings::LinkSettings(ModulationTypes modulation_type_arg,
                           CodingRates coding_rate_arg)
    : modulation_type(modulation_type_arg),
      coding_rate(coding_rate_arg)
{}

double LinkSettings::getCodingRate() const {
    switch(coding_rate) {
        case wifi80211a::CodingRates::R12: return 1.0 / 2.0;
        case wifi80211a::CodingRates::R23: return 2.0 / 3.0;
        case wifi80211a::CodingRates::R34: return 3.0 / 4.0;
    }
    return 0.0;
}
ModulationTypes LinkSettings::getModulationType() const {return modulation_type;}
int8_t LinkSettings::getErrorCorrectingCode() const {return error_correcting_code;}
int8_t LinkSettings::getNumSubcarriers() const {return num_subcarriers;}
double LinkSettings::getOFDMSymbolDuration() const {return ofdm_symbol_duration;}
double LinkSettings::getOccupiedBandwidth() const {return occupied_bandwidth;}
double LinkSettings::getGuardInterval() const {return guard_interval;}
int8_t LinkSettings::getNumberOfPilots() const {return num_pilots;}
int8_t LinkSettings::getNFFT() const {return nFFT;}
int8_t LinkSettings::getCPLenData() const {return cp_len_data;}
int8_t LinkSettings::getCPLenTraining() const {return cp_len_training;}
double LinkSettings::getT() const{return T;}

int16_t LinkSettings::getNCPBS() const
{
    return bits_per_subcarrier.at(modulation_type) * (num_subcarriers - num_pilots);
}

std::vector<int> LinkSettings::getPilotPositions() const {return pilot_positions;}

bool LinkSettings::changeCodingRate(CodingRates new_coding_rate) {
    coding_rate = new_coding_rate;
    return true;
}
bool LinkSettings::changeModulationType(ModulationTypes new_modulation_type) {
    modulation_type = new_modulation_type;
    return true;
}

} // namespace wifi80211a
