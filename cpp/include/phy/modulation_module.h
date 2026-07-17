//
// Created by Kanishka on 4/7/2026.
//

#ifndef WIFI80211A_MODULATION_MODULE_H
#define WIFI80211A_MODULATION_MODULE_H

#include <complex>
#include <map>
#include <vector>

#include "link_settings.h"
#include "phy/helpers.h"
#include "phy/link_settings.h"

namespace wifi80211a
{

    complexVector map_bits_to_constellation(std::vector<int> bits, ModulationTypes modulation, int n_bpsc);
    std::vector<int> map_constellation_to_bits(const complexVector& symbols, ModulationTypes modulation, int n_bpsc);
    int pack_msb_(const std::vector<int>& bits, ModulationTypes modulation);
    std::vector<int> unpack_msb_(int key, int n_bpsc);
    
    const std::map<ModulationTypes, double> normalization_constant {
        {ModulationTypes::BPSK, 1.0},
        {ModulationTypes::QPSK, 1.0 / std::sqrt(2.0)},
        {ModulationTypes::QAM16, 1.0 / std::sqrt(10.0)},
        {ModulationTypes::QAM64, 1.0 / std::sqrt(42.0)}
    };

    // Key: bit pattern as integer (MSB first). Value: normalized IQ point.
    // BPSK — K_MOD = 1
    const std::map<int, std::complex<double>> bpsk_map = {
        {0, {-1.0,  0.0}},
        {1, { 1.0,  0.0}},
    };

    // QPSK — K_MOD = 1/sqrt(2)
    // b0b1: b0 -> I, b1 -> Q;  0->-1, 1->+1 per axis
    const std::map<int, std::complex<double>> qpsk_map = {
        {0b00, {-1.0, -1.0}},   // I=-1, Q=-1
        {0b01, {-1.0,  1.0}},   // I=-1, Q=+1
        {0b10, { 1.0, -1.0}},   // I=+1, Q=-1
        {0b11, { 1.0,  1.0}},   // I=+1, Q=+1
        // caller must multiply by 1/sqrt(2)
    };

    // 16-QAM — K_MOD = 1/sqrt(10)
    // b0b1 -> I, b2b3 -> Q using 2-bit Gray: 00->-3, 01->-1, 10->+3, 11->+1
    const std::map<int, std::complex<double>> qam16_map = {
        {0b0000, {-3.0, -3.0}}, {0b0001, {-3.0, -1.0}},
        {0b0010, {-3.0,  3.0}}, {0b0011, {-3.0,  1.0}},
        {0b0100, {-1.0, -3.0}}, {0b0101, {-1.0, -1.0}},
        {0b0110, {-1.0,  3.0}}, {0b0111, {-1.0,  1.0}},
        {0b1000, { 3.0, -3.0}}, {0b1001, { 3.0, -1.0}},
        {0b1010, { 3.0,  3.0}}, {0b1011, { 3.0,  1.0}},
        {0b1100, { 1.0, -3.0}}, {0b1101, { 1.0, -1.0}},
        {0b1110, { 1.0,  3.0}}, {0b1111, { 1.0,  1.0}},
        // caller must multiply by 1/sqrt(10)
    };

    // 64-QAM — K_MOD = 1/sqrt(42)
    // b0b1b2 -> I, b3b4b5 -> Q using 3-bit Gray:
    //   000->-7, 001->-5, 010->-1, 011->-3, 100->+7, 101->+5, 110->+1, 111->+3
    const std::map<int, std::complex<double>> qam64_map = {
        {0b000000, {-7.0, -7.0}}, {0b000001, {-7.0, -5.0}},
        {0b000010, {-7.0, -1.0}}, {0b000011, {-7.0, -3.0}},
        {0b000100, {-7.0,  7.0}}, {0b000101, {-7.0,  5.0}},
        {0b000110, {-7.0,  1.0}}, {0b000111, {-7.0,  3.0}},
        {0b001000, {-5.0, -7.0}}, {0b001001, {-5.0, -5.0}},
        {0b001010, {-5.0, -1.0}}, {0b001011, {-5.0, -3.0}},
        {0b001100, {-5.0,  7.0}}, {0b001101, {-5.0,  5.0}},
        {0b001110, {-5.0,  1.0}}, {0b001111, {-5.0,  3.0}},
        {0b010000, {-1.0, -7.0}}, {0b010001, {-1.0, -5.0}},
        {0b010010, {-1.0, -1.0}}, {0b010011, {-1.0, -3.0}},
        {0b010100, {-1.0,  7.0}}, {0b010101, {-1.0,  5.0}},
        {0b010110, {-1.0,  1.0}}, {0b010111, {-1.0,  3.0}},
        {0b011000, {-3.0, -7.0}}, {0b011001, {-3.0, -5.0}},
        {0b011010, {-3.0, -1.0}}, {0b011011, {-3.0, -3.0}},
        {0b011100, {-3.0,  7.0}}, {0b011101, {-3.0,  5.0}},
        {0b011110, {-3.0,  1.0}}, {0b011111, {-3.0,  3.0}},
        {0b100000, { 7.0, -7.0}}, {0b100001, { 7.0, -5.0}},
        {0b100010, { 7.0, -1.0}}, {0b100011, { 7.0, -3.0}},
        {0b100100, { 7.0,  7.0}}, {0b100101, { 7.0,  5.0}},
        {0b100110, { 7.0,  1.0}}, {0b100111, { 7.0,  3.0}},
        {0b101000, { 5.0, -7.0}}, {0b101001, { 5.0, -5.0}},
        {0b101010, { 5.0, -1.0}}, {0b101011, { 5.0, -3.0}},
        {0b101100, { 5.0,  7.0}}, {0b101101, { 5.0,  5.0}},
        {0b101110, { 5.0,  1.0}}, {0b101111, { 5.0,  3.0}},
        {0b110000, { 1.0, -7.0}}, {0b110001, { 1.0, -5.0}},
        {0b110010, { 1.0, -1.0}}, {0b110011, { 1.0, -3.0}},
        {0b110100, { 1.0,  7.0}}, {0b110101, { 1.0,  5.0}},
        {0b110110, { 1.0,  1.0}}, {0b110111, { 1.0,  3.0}},
        {0b111000, { 3.0, -7.0}}, {0b111001, { 3.0, -5.0}},
        {0b111010, { 3.0, -1.0}}, {0b111011, { 3.0, -3.0}},
        {0b111100, { 3.0,  7.0}}, {0b111101, { 3.0,  5.0}},
        {0b111110, { 3.0,  1.0}}, {0b111111, { 3.0,  3.0}},
        // caller must multiply by 1/sqrt(42)
    };
}

#endif //WIFI80211A_MODULATION_MODULE_H
