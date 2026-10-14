// SPDX-License-Identifier: MIT
#include "../driver/include/ph_hda_spec.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
static void check(int condition, const char* message)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

int main()
{
    uint32_t command = 0;
    uint16_t encoded = 0;
    PhHdaPcmFormat f = {48000, 16, 16, 2};
    check(PhHdaEncodeVerb8(2, 17, 0xf00, 9, &command) && command == 0x211f0009,
        "known GetParameter encoding");
    check(PhHdaEncodeVerb16(0, 2, 2, 0x4011, &command) && command == 0x00224011,
        "known converter format verb encoding");
    check(!PhHdaEncodeVerb8(15, 0, 0xf00, 0, &command), "no broadcast commands");
    check(!PhHdaEncodeVerb8(16, 0, 0xf00, 0, &command), "codec overflow");
    check(!PhHdaEncodeVerb8(0, 128, 0xf00, 0, &command), "no indirect node addressing");
    check(!PhHdaEncodeVerb8(0, 0, 0x1000, 0, &command), "verb8 overflow");
    check(!PhHdaEncodeVerb8(0, 0, 0xf00, 256, &command), "payload8 overflow");
    check(!PhHdaEncodeVerb16(0, 0, 16, 0, &command), "verb16 overflow");
    check(!PhHdaEncodeVerb16(0, 0, 2, 0x10000, &command), "payload16 overflow");
    check(!PhHdaEncodeVerb8(0, 0, 0, 0, 0), "null command output");
    check(PhHdaEncodePcmFormat(&f, &encoded) && encoded == 0x0011, "48k16 stereo");
    f.sample_rate = 44100;
    check(PhHdaEncodePcmFormat(&f, &encoded) && encoded == 0x4011, "44.1k family");
    f.sample_rate = 32000;
    check(PhHdaEncodePcmFormat(&f, &encoded) && encoded == 0x0a11, "32k exact2/3 rate");
    f = PhHdaPcmFormat{96000, 24, 32, 2};
    check(PhHdaEncodePcmFormat(&f, &encoded) && encoded == 0x0831, "96k24 in32");
    f.container_bits = 24;
    check(!PhHdaEncodePcmFormat(&f, &encoded), "reject packed24 memory layout");
    f = PhHdaPcmFormat{48001, 16, 16, 2};
    encoded = 0xbeef;
    check(!PhHdaEncodePcmFormat(&f, &encoded) && encoded == 0xbeef, "rate cannot round");
    f.sample_rate = 384000;
    check(!PhHdaEncodePcmFormat(&f, &encoded), "reserved multiplier8");
    f = PhHdaPcmFormat{48000, 16, 16, 0};
    check(!PhHdaEncodePcmFormat(&f, &encoded), "zero channels");
    f.channels = 17;
    check(!PhHdaEncodePcmFormat(&f, &encoded), "channels overflow");
    f = PhHdaPcmFormat{48000, 12, 16, 2};
    check(!PhHdaEncodePcmFormat(&f, &encoded), "unsupported sample width");
    check(!PhHdaDecodePcmFormat(0x8011, &f), "nonPCM cannot decode asPCM");
    check(!PhHdaDecodePcmFormat(0x0091, &f), "reserved format bit7");
    check(!PhHdaDecodePcmFormat(0x2011, &f), "reserved multiplier5");
    check(!PhHdaDecodePcmFormat(0x0051, &f), "reserved sample width code");
    check(!PhHdaDecodePcmFormat(0x4711, &f), "fractional Hz rejected");

    // Exhaust all raw16-bit formats: every accepted encoding preserves rate,
    // precision, memory container and channels when canonicalized and decoded.
    for (uint32_t word = 0; word <= 0xffff; ++word) {
        PhHdaPcmFormat a, b;
        if (PhHdaDecodePcmFormat((uint16_t)word, &a)) {
            check(PhHdaEncodePcmFormat(&a, &encoded), "accepted format re-encodes");
            check(PhHdaDecodePcmFormat(encoded, &b) && a.sample_rate == b.sample_rate &&
                a.valid_bits == b.valid_bits && a.container_bits == b.container_bits &&
                a.channels == b.channels, "encoding preserves format meaning");
        }
    }
    const uint32_t stereo_digital = 0x00000201;
    const uint32_t caps = (1u << 6) | (1u << 5) | (1u << 17) | (1u << 19);
    f = PhHdaPcmFormat{48000, 24, 32, 2};
    check(PhHdaConverterSupportsPcm(stereo_digital, caps, 1, &f, 1), "SPDIF24 actual caps");
    check(!PhHdaConverterSupportsPcm(stereo_digital, caps & ~(1u << 19), 1, &f, 1),
        "valid24 capability required despite32 container");
    check(!PhHdaConverterSupportsPcm(stereo_digital, caps, 0, &f, 1), "PCM bit required");
    check(!PhHdaConverterSupportsPcm(1, caps, 1, &f, 1), "SPDIF needs digital converter");
    f.sample_rate = 96000;
    check(!PhHdaConverterSupportsPcm(stereo_digital, caps, 1, &f, 1), "rate cap required");
    f = PhHdaPcmFormat{48000, 32, 32, 2};
    check(!PhHdaConverterSupportsPcm(stereo_digital, caps | (1u << 20), 1, &f, 1),
        "SPDIF32 precision rejected");
    f = PhHdaPcmFormat{48000, 16, 16, 8};
    check(!PhHdaConverterSupportsPcm(stereo_digital, caps, 1, &f, 0), "individualDAC limit");
    const uint32_t eight_channel = 0x00006001;
    check(PhHdaWidgetChannels(eight_channel) == 8, "extended widget channel field");
    check(PhHdaConverterSupportsPcm(eight_channel, caps, 1, &f, 0), "8channel analog converter");
    check(!PhHdaConverterSupportsPcm(eight_channel | (1u << 9), caps, 1, &f, 1),
        "SPDIF cannot expose unencoded8channelPCM");
    printf("PASS: %u HD Audio protocol checks\n", checks);
    return 0;
}
