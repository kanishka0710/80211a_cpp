"""802.11a-ish DCF timing and contention constants (microseconds)."""

# IEEE 802.11a OFDM PHY timing (Clause 17 / DCF defaults).
SIFS_US: int = 16
SLOT_US: int = 9
DIFS_US: int = SIFS_US + 2 * SLOT_US  # 34 µs

CW_MIN: int = 15
CW_MAX: int = 1023

# Short retry limit (dot11ShortRetryLimit) for frames that expect an ACK.
SHORT_RETRY_LIMIT: int = 7

# Post-TX ACK wait (added on top of data airtime). Must cover SIFS + ACK
# PPDU airtime (~24–40 µs at 6 Mb/s for a short ACK) plus slack.
ACK_TIMEOUT_US: int = SIFS_US + 200

# Sample rate of the underlying 802.11a PHY (not tunable in this project).
PHY_SAMPLE_RATE_HZ: float = 20e6
