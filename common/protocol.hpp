#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <optional>
#include <arpa/inet.h>
#include <nlohmann/json.hpp>

// Custom TCP telemetry protocol (DNIIP wire format).
//
// Frame layout on the wire, all integers big-endian:
//   [ uint32_t  magic      ]  0x444E4949  ("DNII")
//   [ uint8_t   type       ]  MessageType
//   [ uint32_t  payload_len]  length of payload_len bytes that follow
//   [ payload   bytes      ]  UTF-8 JSON payload
//
// A fixed magic + explicit length prefix lets the epoll server frame
// messages off a streaming, possibly-coalesced/fragmented TCP socket
// without relying on delimiters that could appear inside JSON strings.

namespace dniip::protocol {

constexpr uint32_t kMagic = 0x444E4949;
constexpr size_t kHeaderSize = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t);
constexpr uint32_t kMaxPayloadSize = 16u * 1024 * 1024; // 16 MiB guard against bad/malicious length

enum class MessageType : uint8_t {
    kTelemetry = 1,
    kHeartbeat = 2,
    kAck = 3,
    kRegister = 4,
    kRegisterAck = 5,
};

struct FrameHeader {
    uint32_t magic = kMagic;
    MessageType type{};
    uint32_t payload_len = 0;
};

// Serializes a header + JSON payload into a byte buffer ready for send().
inline std::string encode_frame(MessageType type, const nlohmann::json& payload) {
    const std::string body = payload.dump();
    std::string out;
    out.resize(kHeaderSize + body.size());

    uint32_t magic_be = htonl(kMagic);
    uint8_t type_byte = static_cast<uint8_t>(type);
    uint32_t len_be = htonl(static_cast<uint32_t>(body.size()));

    size_t off = 0;
    memcpy(out.data() + off, &magic_be, sizeof(magic_be)); off += sizeof(magic_be);
    memcpy(out.data() + off, &type_byte, sizeof(type_byte)); off += sizeof(type_byte);
    memcpy(out.data() + off, &len_be, sizeof(len_be)); off += sizeof(len_be);
    memcpy(out.data() + off, body.data(), body.size());

    return out;
}

// Attempts to parse a header from the front of `buf`. Returns std::nullopt if
// fewer than kHeaderSize bytes are available.
inline std::optional<FrameHeader> try_parse_header(const std::string& buf) {
    if (buf.size() < kHeaderSize) return std::nullopt;

    uint32_t magic_be, len_be;
    uint8_t type_byte;
    size_t off = 0;
    memcpy(&magic_be, buf.data() + off, sizeof(magic_be)); off += sizeof(magic_be);
    memcpy(&type_byte, buf.data() + off, sizeof(type_byte)); off += sizeof(type_byte);
    memcpy(&len_be, buf.data() + off, sizeof(len_be)); off += sizeof(len_be);

    FrameHeader hdr;
    hdr.magic = ntohl(magic_be);
    hdr.type = static_cast<MessageType>(type_byte);
    hdr.payload_len = ntohl(len_be);
    return hdr;
}

} // namespace dniip::protocol
