/**
 * @file sx1262_regs.h
 * @brief SX1262 opcodes, register addresses, and IRQ flags
 *
 * Values from Semtech SX1261/62 datasheet DS.SX1261-2.W.APP (Rev 2.1)
 * and RadioLib (github.com/jgromes/RadioLib) for errata workarounds.
 */
#pragma once

// ---------------------------------------------------------------------------
// SPI command opcodes
// ---------------------------------------------------------------------------

// Operational modes
#define SX1262_CMD_SET_SLEEP                0x84
#define SX1262_CMD_SET_STANDBY              0x80
#define SX1262_CMD_SET_FS                   0xC1
#define SX1262_CMD_SET_TX                   0x83
#define SX1262_CMD_SET_RX                   0x82
#define SX1262_CMD_SET_RX_DUTY_CYCLE        0x94
#define SX1262_CMD_SET_CAD                  0xC5
#define SX1262_CMD_SET_TX_CONTINUOUS_WAVE    0xD1
#define SX1262_CMD_SET_TX_INFINITE_PREAMBLE 0xD2
#define SX1262_CMD_SET_REGULATOR_MODE       0x96
#define SX1262_CMD_CALIBRATE                0x89
#define SX1262_CMD_CALIBRATE_IMAGE          0x98
#define SX1262_CMD_SET_PA_CONFIG            0x95
#define SX1262_CMD_SET_RX_TX_FALLBACK_MODE  0x93

// RF configuration
#define SX1262_CMD_SET_RF_FREQUENCY         0x86
#define SX1262_CMD_SET_PACKET_TYPE          0x8A
#define SX1262_CMD_GET_PACKET_TYPE          0x11
#define SX1262_CMD_SET_TX_PARAMS            0x8E
#define SX1262_CMD_SET_MODULATION_PARAMS    0x8B
#define SX1262_CMD_SET_PACKET_PARAMS        0x8C
#define SX1262_CMD_SET_CAD_PARAMS           0x88
#define SX1262_CMD_SET_LORA_SYMB_NUM_TIMEOUT 0xA0

// Buffer
#define SX1262_CMD_SET_BUFFER_BASE_ADDRESS  0x8F
#define SX1262_CMD_WRITE_REGISTER           0x0D
#define SX1262_CMD_READ_REGISTER            0x1D
#define SX1262_CMD_WRITE_BUFFER             0x0E
#define SX1262_CMD_READ_BUFFER              0x1E

// DIO and IRQ
#define SX1262_CMD_SET_DIO_IRQ_PARAMS       0x08
#define SX1262_CMD_GET_IRQ_STATUS           0x12
#define SX1262_CMD_CLEAR_IRQ_STATUS         0x02
#define SX1262_CMD_SET_DIO2_AS_RF_SWITCH    0x9D
#define SX1262_CMD_SET_DIO3_AS_TCXO_CTRL    0x97

// Status
#define SX1262_CMD_GET_STATUS               0xC0
#define SX1262_CMD_GET_RSSI_INST            0x15
#define SX1262_CMD_GET_RX_BUFFER_STATUS     0x13
#define SX1262_CMD_GET_PACKET_STATUS        0x14
#define SX1262_CMD_GET_DEVICE_ERRORS        0x17
#define SX1262_CMD_CLEAR_DEVICE_ERRORS      0x07
#define SX1262_CMD_GET_STATS                0x10
#define SX1262_CMD_RESET_STATS              0x00

// Filler byte for SPI read transactions (MOSI don't-care during read phase)
#define SX1262_SPI_FILLER                   0x00

// ---------------------------------------------------------------------------
// IRQ flags (16-bit bitmask)
// ---------------------------------------------------------------------------

#define SX1262_IRQ_TX_DONE                  0x0001
#define SX1262_IRQ_RX_DONE                  0x0002
#define SX1262_IRQ_PREAMBLE_DETECTED        0x0004
#define SX1262_IRQ_SYNC_WORD_VALID          0x0008
#define SX1262_IRQ_HEADER_VALID             0x0010
#define SX1262_IRQ_HEADER_ERR               0x0020
#define SX1262_IRQ_CRC_ERR                  0x0040
#define SX1262_IRQ_CAD_DONE                 0x0080
#define SX1262_IRQ_CAD_DETECTED             0x0100
#define SX1262_IRQ_TIMEOUT                  0x0200
#define SX1262_IRQ_ALL                      0x03FF
#define SX1262_IRQ_NONE                     0x0000

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Standby modes
#define SX1262_STANDBY_RC                   0x00
#define SX1262_STANDBY_XOSC                 0x01

// Packet types
#define SX1262_PACKET_TYPE_GFSK             0x00
#define SX1262_PACKET_TYPE_LORA             0x01

// Regulator modes
#define SX1262_REGULATOR_LDO                0x00
#define SX1262_REGULATOR_DCDC               0x01

// PA config device select
#define SX1262_PA_CONFIG_SX1262             0x00

// Sleep config
#define SX1262_SLEEP_COLD_START             0x00
#define SX1262_SLEEP_WARM_START             0x04

// TX timeout (no timeout)
#define SX1262_TX_TIMEOUT_NONE              0x000000

// Frequency step: 32 MHz / 2^25
#define SX1262_FREQ_STEP                    0.9536743164f

// Max payload
#define SX1262_MAX_PACKET_LENGTH            255

// LoRa bandwidth values (register encoding)
#define SX1262_LORA_BW_125                  0x04
#define SX1262_LORA_BW_250                  0x05
#define SX1262_LORA_BW_500                  0x06

// LoRa header type
#define SX1262_LORA_HEADER_EXPLICIT         0x00
#define SX1262_LORA_HEADER_IMPLICIT         0x01

// LoRa CRC
#define SX1262_LORA_CRC_OFF                 0x00
#define SX1262_LORA_CRC_ON                  0x01

// LoRa IQ
#define SX1262_LORA_IQ_STANDARD             0x00
#define SX1262_LORA_IQ_INVERTED             0x01

// TX ramp time
#define SX1262_TX_RAMP_200US                0x04

// Calibration bitmask (all blocks)
#define SX1262_CALIBRATE_ALL                0x7F

// ---------------------------------------------------------------------------
// Register addresses (for errata workarounds)
// ---------------------------------------------------------------------------

#define SX1262_REG_TX_CLAMP_CONFIG          0x08D8
#define SX1262_REG_IQ_POLARITY_SETUP        0x0736
#define SX1262_REG_LORA_SYNC_WORD_MSB       0x0740
#define SX1262_REG_LORA_SYNC_WORD_LSB       0x0741

// LoRa sync word values
#define SX1262_LORA_SYNC_WORD_PUBLIC        0x3444
#define SX1262_LORA_SYNC_WORD_PRIVATE       0x1424

// ---------------------------------------------------------------------------
// Image calibration frequency pairs (per datasheet Table 9-2)
// ---------------------------------------------------------------------------

#define SX1262_CAL_IMG_430_MHZ_1            0x6B
#define SX1262_CAL_IMG_430_MHZ_2            0x6F
#define SX1262_CAL_IMG_470_MHZ_1            0x75
#define SX1262_CAL_IMG_470_MHZ_2            0x81
#define SX1262_CAL_IMG_779_MHZ_1            0xC1
#define SX1262_CAL_IMG_779_MHZ_2            0xC5
#define SX1262_CAL_IMG_863_MHZ_1            0xD7
#define SX1262_CAL_IMG_863_MHZ_2            0xDB
#define SX1262_CAL_IMG_902_MHZ_1            0xE1
#define SX1262_CAL_IMG_902_MHZ_2            0xE9
