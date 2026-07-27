from ast import Module
from dataclasses import dataclass, field


@dataclass
class ModulationTypes:
    BPSK = "BPSK"
    QPSK = "QPSK"
    QAM16 = "QAM16"
    QAM64 = "QAM64"

@dataclass
class CodingRates:
    R12 = "R12"
    R23 = "R23"
    R34 = "R34"

rateMap = {
    1 : (ModulationTypes.BPSK, CodingRates.R12),
    2 : (ModulationTypes.BPSK, CodingRates.R34),
    3 : (ModulationTypes.QPSK, CodingRates.R12),
    4 : (ModulationTypes.QPSK, CodingRates.R34),
    5 : (ModulationTypes.QAM16, CodingRates.R12),
    6 : (ModulationTypes.QAM16, CodingRates.R34),
    7 : (ModulationTypes.QAM64, CodingRates.R23),
    8 : (ModulationTypes.QAM64, CodingRates.R34),
}

rateMapInverse = {
    (ModulationTypes.BPSK, CodingRates.R12): 1,
    (ModulationTypes.BPSK, CodingRates.R34): 2,
    (ModulationTypes.QPSK, CodingRates.R12): 3,
    (ModulationTypes.QPSK, CodingRates.R34): 4,
    (ModulationTypes.QAM16, CodingRates.R12): 5,
    (ModulationTypes.QAM16, CodingRates.R34): 6,
    (ModulationTypes.QAM64, CodingRates.R23): 7,
    (ModulationTypes.QAM64, CodingRates.R34): 8,
}

# IEEE 802.11a-1999 17.3.4.1 (Table 78): the SIGNAL field's 4-bit RATE
# codeword for each rate is a fixed, non-sequential code -- NOT a binary
# index of rateMap's keys. Packed LSB-first (bit0=R1 ... bit3=R4) to match
# int_to_bits()/decode_signal_header()'s bit order. E.g. 36 Mb/s (16-QAM,
# R=3/4) transmits R1..R4 = 1,0,1,1 per Annex G Table G.7.
rateCodeword = {
    1: 0b1011,  # 6 Mb/s:  R1..R4 = 1,1,0,1
    2: 0b1111,  # 9 Mb/s:  R1..R4 = 1,1,1,1
    3: 0b1010,  # 12 Mb/s: R1..R4 = 0,1,0,1
    4: 0b1110,  # 18 Mb/s: R1..R4 = 0,1,1,1
    5: 0b1001,  # 24 Mb/s: R1..R4 = 1,0,0,1
    6: 0b1101,  # 36 Mb/s: R1..R4 = 1,0,1,1
    7: 0b1000,  # 48 Mb/s: R1..R4 = 0,0,0,1
    8: 0b1100,  # 54 Mb/s: R1..R4 = 0,0,1,1
}

rateCodewordToValue = {codeword: value for value, codeword in rateCodeword.items()}


@dataclass
class LinkSettings:
    modulationType: str = ModulationTypes.BPSK
    errorCorrectingCode: int = 7
    numSubcarriers: int = 52
    numPilots: int = 4
    nFFT: int = 64
    cpLenData: int = 16
    cpLenTraining: int = 32
    ofdmSymbolDuration: float = 4e-6
    guardInterval: float = 0.8e-6
    occupiedBandwidth: float = 16.6e6
    pilotPositions: list = field(default_factory=lambda: [-21, -7, 7, 21])
    bitPerSubcarrier: dict = field(default_factory=lambda: {
        ModulationTypes.BPSK: 1,
        ModulationTypes.QPSK: 2,
        ModulationTypes.QAM16: 4,
        ModulationTypes.QAM64: 6,
    })
    sampleRate: float = 20e6 # Sample Rate for 802.11a, NOT TUNABLE

    def __post_init__(self):
        self.T = self.ofdmSymbolDuration - self.guardInterval
        self.NCPBS = self.bitPerSubcarrier[self.modulationType] * (self.numSubcarriers - self.numPilots)
