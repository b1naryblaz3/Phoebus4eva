// SPDX-License-Identifier: MIT
// Original protocol helpers. No vendor source or proprietary control sequence.
// Intel HD Audio 1.0a: 3.7, 3.7.1, 7.1.2, 7.3.4.6-.8.
#ifndef PH_HDA_SPEC_H
#define PH_HDA_SPEC_H

#include "ph_types.h"

enum PhHdaVerb {
    PhHdaGetParameter = 0xf00,
    PhHdaGetConnectionSelect = 0xf01,
    PhHdaGetConnectionListEntry = 0xf02,
    PhHdaGetPowerState = 0xf05,
    PhHdaGetConverterStreamChannel = 0xf06,
    PhHdaGetPinWidgetControl = 0xf07,
    PhHdaGetDigitalConverterControl = 0xf0d,
    PhHdaGetConfigurationDefault = 0xf1c,
    PhHdaSetConnectionSelect = 0x701,
    PhHdaSetPowerState = 0x705,
    PhHdaSetConverterStreamChannel = 0x706,
    PhHdaSetPinWidgetControl = 0x707,
    PhHdaSetDigitalConverterControl1 = 0x70d,
    PhHdaFunctionReset = 0x7ff
};

enum PhHdaParameter {
    PhHdaVendorId = 0x00,
    PhHdaSubordinateNodeCount = 0x04,
    PhHdaFunctionGroupType = 0x05,
    PhHdaAudioFunctionGroupCapabilities = 0x08,
    PhHdaAudioWidgetCapabilities = 0x09,
    PhHdaPcmSizeRates = 0x0a,
    PhHdaStreamFormats = 0x0b,
    PhHdaPinCapabilities = 0x0c,
    PhHdaConnectionListLength = 0x0e,
    PhHdaSupportedPowerStates = 0x0f
};

typedef struct PhHdaPcmFormat {
    uint32_t sample_rate;
    uint16_t valid_bits;
    uint16_t container_bits;
    uint16_t channels;
} PhHdaPcmFormat;

// Direct node addressing only. Address15 is broadcast, deliberately excluded.
static inline int PhHdaEncodeVerb8(uint32_t codec, uint32_t node,
    uint32_t verb, uint32_t payload, uint32_t* command)
{
    if (!command || codec > 14 || node > 127 || verb > 0xfff || payload > 0xff)
        return 0;
    *command = (codec << 28) | (node << 20) | (verb << 8) | payload;
    return 1;
}

static inline int PhHdaEncodeVerb16(uint32_t codec, uint32_t node,
    uint32_t verb, uint32_t payload, uint32_t* command)
{
    if (!command || codec > 14 || node > 127 || verb > 0xf || payload > 0xffff)
        return 0;
    *command = (codec << 28) | (node << 20) | (verb << 16) | payload;
    return 1;
}

static inline uint16_t PhHdaContainerBits(uint16_t valid_bits)
{
    switch (valid_bits) {
    case 8: return 8;
    case 16: return 16;
    case 20: case 24: case 32: return 32;
    default: return 0;
    }
}

static inline int PhHdaSampleWidthCode(uint16_t valid_bits)
{
    switch (valid_bits) {
    case 8: return 0;
    case 16: return 1;
    case 20: return 2;
    case 24: return 3;
    case 32: return 4;
    default: return -1;
    }
}

// A representable format is not necessarily supported by a particular codec.
// Production code must use the bus-returned ConverterFormat for programming.
static inline int PhHdaEncodePcmFormat(const PhHdaPcmFormat* f, uint16_t* encoded)
{
    int width;
    uint32_t family, multiple, divisor;
    if (!f || !encoded || f->channels < 1 || f->channels > 16 || !f->sample_rate)
        return 0;
    width = PhHdaSampleWidthCode(f->valid_bits);
    if (width < 0 || f->container_bits != PhHdaContainerBits(f->valid_bits))
        return 0;
    // Prefer the smallest divisor, then multiplier; never round a sample rate.
    for (family = 0; family < 2; ++family) {
        const uint32_t base = family ? 44100u : 48000u;
        for (divisor = 1; divisor <= 8; ++divisor) {
            for (multiple = 1; multiple <= 4; ++multiple) {
                const uint32_t numerator = base * multiple;
                if (numerator % divisor == 0 && numerator / divisor == f->sample_rate) {
                    *encoded = (uint16_t)((family << 14) | ((multiple - 1) << 11) |
                        ((divisor - 1) << 8) | ((uint32_t)width << 4) | (f->channels - 1));
                    return 1;
                }
            }
        }
    }
    return 0;
}

static inline int PhHdaDecodePcmFormat(uint16_t encoded, PhHdaPcmFormat* f)
{
    static const uint16_t widths[5] = { 8, 16, 20, 24, 32 };
    const uint32_t width = (encoded >> 4) & 7u;
    const uint32_t multiple = ((encoded >> 11) & 7u) + 1;
    const uint32_t divisor = ((encoded >> 8) & 7u) + 1;
    const uint32_t numerator = ((encoded & 0x4000) ? 44100u : 48000u) * multiple;
    PhHdaPcmFormat result;
    if (!f || (encoded & 0x8080) || width > 4 || multiple > 4 || numerator % divisor)
        return 0;
    result.sample_rate = numerator / divisor;
    result.valid_bits = widths[width];
    result.container_bits = PhHdaContainerBits(result.valid_bits);
    result.channels = (uint16_t)((encoded & 15u) + 1);
    *f = result;
    return 1;
}

// Parameter0x0a bits0..11. A capability bit does not bypass format validation.
static inline uint32_t PhHdaPcmRateMask(uint32_t sample_rate)
{
    switch (sample_rate) {
    case 8000: return 1u << 0;
    case 11025: return 1u << 1;
    case 16000: return 1u << 2;
    case 22050: return 1u << 3;
    case 32000: return 1u << 4;
    case 44100: return 1u << 5;
    case 48000: return 1u << 6;
    case 88200: return 1u << 7;
    case 96000: return 1u << 8;
    case 176400: return 1u << 9;
    case 192000: return 1u << 10;
    case 384000: return 1u << 11;
    default: return 0;
    }
}

static inline uint32_t PhHdaPcmWidthMask(uint16_t valid_bits)
{
    const int code = PhHdaSampleWidthCode(valid_bits);
    return code < 0 ? 0 : (1u << (16 + (uint32_t)code));
}

static inline uint16_t PhHdaWidgetChannels(uint32_t widget_capabilities)
{
    return (uint16_t)(1 + (widget_capabilities & 1u) +
        (((widget_capabilities >> 13) & 7u) << 1));
}

// Check a single converter. An endpoint using several converters must check
// each converter's assigned channels and every widget in the selected route.
static inline int PhHdaConverterSupportsPcm(uint32_t widget_capabilities,
    uint32_t pcm_size_rates, uint32_t stream_formats,
    const PhHdaPcmFormat* f, int spdif)
{
    uint16_t encoded;
    uint32_t type, rate, width;
    if (!PhHdaEncodePcmFormat(f, &encoded)) return 0;
    type = (widget_capabilities >> 20) & 15u;
    if (type > 1 || !(stream_formats & 1u) ||
        f->channels > PhHdaWidgetChannels(widget_capabilities)) return 0;
    rate = PhHdaPcmRateMask(f->sample_rate);
    width = PhHdaPcmWidthMask(f->valid_bits);
    if (!rate || !width || !(pcm_size_rates & rate) || !(pcm_size_rates & width)) return 0;
    if (spdif && (!(widget_capabilities & (1u << 9)) || f->channels != 2 ||
        (f->valid_bits != 16 && f->valid_bits != 20 && f->valid_bits != 24))) return 0;
    return 1;
}

#endif
