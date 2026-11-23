#include "rhodes/instrument_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

constexpr rhodes::EncodedInstrumentState golden{
    0x52, 0x48, 0x53, 0x54, 1, 0, 1, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f};

void replace_float(rhodes::EncodedInstrumentState& bytes, std::size_t at, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    bytes[at] = static_cast<std::uint8_t>(bits & 255U);
    bytes[at + 1] = static_cast<std::uint8_t>((bits >> 8U) & 255U);
    bytes[at + 2] = static_cast<std::uint8_t>((bits >> 16U) & 255U);
    bytes[at + 3] = static_cast<std::uint8_t>((bits >> 24U) & 255U);
}

void codec_golden() {
    require(rhodes::encode_instrument_state({0.5F, true}) == golden, "v1 golden bytes differ");
    require(rhodes::decode_instrument_state(golden) == rhodes::InstrumentState{0.5F, true},
            "golden state decode differs");
    for (const float gain : {0.0F, 0.001F, 0.25F, 0.5F, 1.0F}) {
        for (const bool pedal : {false, true}) {
            const rhodes::InstrumentState expected{gain, pedal};
            const auto encoded = rhodes::encode_instrument_state(expected);
            require(encoded.has_value(), "valid state rejected");
            require(rhodes::decode_instrument_state(*encoded) == expected, "state round trip differs");
        }
    }
    auto negative_zero = golden;
    replace_float(negative_zero, 8, -0.0F);
    replace_float(negative_zero, 12, -0.0F);
    const auto decoded = rhodes::decode_instrument_state(negative_zero);
    require(decoded.has_value() && std::bit_cast<std::uint32_t>(decoded->monitoring_gain) == 0,
            "decoded signed zero is not canonical");
    require(rhodes::encode_instrument_state({-0.0F, false}) ==
            rhodes::encode_instrument_state({0.0F, false}), "encoded signed zero differs");
}

void codec_rejection() {
    for (std::size_t size = 0; size < golden.size(); ++size) {
        require(!rhodes::decode_instrument_state(std::span{golden}.first(size)), "truncation accepted");
    }
    std::array<std::uint8_t, 17> extended{};
    std::copy(golden.begin(), golden.end(), extended.begin());
    require(!rhodes::decode_instrument_state(extended), "trailing byte accepted");
    for (std::size_t at = 0; at < 8; ++at) {
        auto bad = golden;
        bad[at] ^= 0x80U;
        require(!rhodes::decode_instrument_state(bad), "invalid magic/version/model accepted");
    }
    for (const float value : {-1.0F, 1.0001F, std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = golden;
        replace_float(bad, 8, value);
        require(!rhodes::decode_instrument_state(bad), "invalid gain bytes accepted");
        require(!rhodes::encode_instrument_state({value, true}), "invalid gain encoded");
    }
    for (const float value : {-1.0F, 0.5F, 2.0F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = golden;
        replace_float(bad, 12, value);
        require(!rhodes::decode_instrument_state(bad), "invalid pedal accepted");
    }
}

void mailbox_recall() {
    rhodes::StateMailbox mailbox;
    require(mailbox.snapshot() == rhodes::InstrumentState{}, "incorrect default snapshot");
    require(mailbox.request_recall({0.2F, true}), "first recall rejected");
    require(mailbox.request_recall({0.8F, false}), "coalesced recall rejected");
    require(mailbox.snapshot() == rhodes::InstrumentState{0.8F, false}, "save missed pending recall");
    const auto token = mailbox.begin_block();
    require(token.apply_recall && token.may_publish &&
            token.recalled == rhodes::InstrumentState{0.8F, false}, "wrong recall consumed");
    require(mailbox.end_block(token, {0.6F, true}), "normal automation publication failed");
    require(mailbox.snapshot() == rhodes::InstrumentState{0.6F, true}, "wrong automation snapshot");
    require(!mailbox.begin_block().apply_recall, "consumed recall repeated");
    const auto before = mailbox.snapshot();
    require(!mailbox.request_recall({std::numeric_limits<float>::quiet_NaN(), false}),
            "nonfinite recall accepted");
    require(mailbox.snapshot() == before, "invalid recall disturbed snapshot");
    require(!mailbox.end_block({}, {0.9F, false}), "invalid publication token accepted");
}

void mailbox_races() {
    rhodes::StateMailbox mailbox;
    const auto old = mailbox.begin_block();
    require(mailbox.request_recall({0.9F, true}), "UI recall failed");
    require(!mailbox.end_block(old, {0.1F, false}), "automation overwrote newer recall");
    const auto first = mailbox.begin_block();
    require(first.apply_recall && first.recalled == rhodes::InstrumentState{0.9F, true},
            "newer recall was lost");
    // Identical data must still be observed when requested after consumption.
    require(mailbox.request_recall({0.9F, true}), "identical later recall rejected");
    require(!mailbox.end_block(first, {0.3F, false}), "identical mid-block recall was lost");
    const auto repeated = mailbox.begin_block();
    require(repeated.apply_recall && repeated.recalled == first.recalled, "identical recall not replayed");
    require(mailbox.end_block(repeated, repeated.recalled), "replayed recall publication failed");
    require(!mailbox.begin_block().apply_recall, "replayed recall never clears");
}

void mailbox_abort() {
    rhodes::StateMailbox mailbox;
    require(mailbox.request_recall({0.4F, true}), "recall failed");
    const auto first = mailbox.begin_block();
    mailbox.abort_block(first);
    const auto retry = mailbox.begin_block();
    require(retry.apply_recall && retry.recalled == first.recalled, "rejected block consumed recall");
    require(mailbox.request_recall({0.7F, false}), "new recall failed");
    mailbox.abort_block(retry);
    require(mailbox.snapshot() == rhodes::InstrumentState{0.7F, false}, "abort overwrote newer recall");
    const auto newer = mailbox.begin_block();
    require(newer.apply_recall && newer.recalled == rhodes::InstrumentState{0.7F, false},
            "newer pending recall disappeared");
    require(mailbox.end_block(newer, newer.recalled), "final publication failed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) { test(); found = true; std::cout << "PASS " << name << '\n'; }
        };
        run("codec_golden", codec_golden);
        run("codec_rejection", codec_rejection);
        run("mailbox_recall", mailbox_recall);
        run("mailbox_races", mailbox_races);
        run("mailbox_abort", mailbox_abort);
        require(found, "unknown state fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
