#include "phy/link_settings.h"

#include <map>

namespace wifi80211a {

LinkSettings::LinkSettings(ModulationTypes modulation_type_arg,
                           CodingRates coding_rate_arg)
    : modulation_type(modulation_type_arg),
      coding_rate(coding_rate_arg)
{}

CodingRates LinkSettings::getCodingRate() const {
    return coding_rate;
}
ModulationTypes LinkSettings::getModulationType() const {return modulation_type;}
int LinkSettings::getErrorCorrectingCode() const {return error_correcting_code;}
int LinkSettings::getNumSubcarriers() const {return num_subcarriers;}
double LinkSettings::getOFDMSymbolDuration() const {return ofdm_symbol_duration;}
double LinkSettings::getOccupiedBandwidth() const {return occupied_bandwidth;}
double LinkSettings::getGuardInterval() const {return guard_interval;}
int LinkSettings::getNumberOfPilots() const {return num_pilots;}
int LinkSettings::getNFFT() const {return nFFT;}
int LinkSettings::getCPLenData() const {return cp_len_data;}
int LinkSettings::getCPLenTraining() const {return cp_len_training;}
double LinkSettings::getT() const{return T;}

int LinkSettings::getNCPBS() const
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
