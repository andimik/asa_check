#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kEtiFrameSize = 6144;
constexpr std::size_t kFibSize = 32;
constexpr std::size_t kFibDataSize = 30;


struct LocationCode {
    uint8_t zone = 0;
    std::vector<uint8_t> digits;
    std::optional<uint16_t> subcodes;
};

struct GeographicBounds {
    double south = 0.0;
    double west = 0.0;
    double north = 0.0;
    double east = 0.0;
};

struct EnsembleTime {
    int year = 0;
    int month = 0;
    int day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint16_t millisecond = 0;
    bool hasSeconds = false;
};

struct AsaMessage {
    bool otherEnsemble = false;
    bool cn = false;
    bool pd = false;
    bool heartbeat = false;
    uint8_t phase = 0;
    uint16_t id = 0;
    std::optional<uint8_t> startSecond;
    std::optional<uint8_t> stage;
    std::optional<uint8_t> incidentId;
    bool last = false;
    uint8_t numFollowing = 0;
    std::vector<LocationCode> locations;
    std::optional<uint32_t> serviceId;
    std::optional<uint8_t> audioServiceComponentType;
    std::optional<EnsembleTime> ensembleTime;
    std::size_t frameIndex = 0;
};

struct AudioService {
    uint32_t serviceId = 0;
    uint8_t subchannelId = 0;
    uint8_t audioServiceComponentType = 0;
};

bool sameMessage(const AsaMessage& left, const AsaMessage& right) {
    if (left.otherEnsemble != right.otherEnsemble || left.cn != right.cn ||
            left.pd != right.pd || left.heartbeat != right.heartbeat ||
            left.phase != right.phase || left.id != right.id ||
            left.startSecond != right.startSecond || left.stage != right.stage ||
            left.incidentId != right.incidentId || left.last != right.last ||
            left.numFollowing != right.numFollowing || left.locations.size() != right.locations.size() ||
            left.serviceId != right.serviceId ||
            left.audioServiceComponentType != right.audioServiceComponentType) {
        return false;
    }

    for (std::size_t i = 0; i < left.locations.size(); ++i) {
        const auto& leftLocation = left.locations[i];
        const auto& rightLocation = right.locations[i];
        if (leftLocation.zone != rightLocation.zone || leftLocation.digits != rightLocation.digits ||
                leftLocation.subcodes != rightLocation.subcodes) {
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> readAll(std::istream& input) {
    std::vector<uint8_t> data;
    char byte = 0;
    while (input.read(&byte, 1) || input.gcount() > 0) {
        data.push_back(static_cast<uint8_t>(byte));
    }
    return data;
}

uint16_t readBe16(const uint8_t* bytes) {
    return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
}

uint32_t readBe32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
           static_cast<uint32_t>(bytes[3]);
}

uint16_t crcCcitt(const uint8_t* bytes, std::size_t size) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint16_t>(bytes[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = static_cast<uint16_t>((crc & 0x8000) != 0
                ? (crc << 1) ^ 0x1021
                : crc << 1);
        }
    }
    return static_cast<uint16_t>(~crc);
}

bool hasEtiSync(const uint8_t* bytes) {
    if (bytes[0] != 0xFF) {
        return false;
    }
    return (bytes[1] == 0x07 && bytes[2] == 0x3A && bytes[3] == 0xB6) ||
           (bytes[1] == 0xF8 && bytes[2] == 0xC5 && bytes[3] == 0x49);
}

std::optional<LocationCode> decodeLocationCode(
        const uint8_t* bytes, std::size_t available, std::size_t& consumed) {
    consumed = 0;
    if (available < 2) {
        return std::nullopt;
    }

    LocationCode location;
    location.zone = static_cast<uint8_t>(bytes[0] & 0x3F);
    const bool hasSubcodes = (bytes[1] & 0x80) != 0;
    const uint8_t numOtherDigits = static_cast<uint8_t>((bytes[1] >> 4) & 0x07);
    const uint8_t firstDigit = static_cast<uint8_t>(bytes[1] & 0x0F);

    if (location.zone > 41 || numOtherDigits > 5 || (hasSubcodes && numOtherDigits > 4)) {
        return std::nullopt;
    }

    const std::size_t digitBytes = (numOtherDigits + 1U) / 2U;
    const std::size_t totalSize = 2U + digitBytes + (hasSubcodes ? 2U : 0U);
    if (totalSize > available) {
        return std::nullopt;
    }

    location.digits.push_back(firstDigit);
    for (uint8_t i = 0; i < numOtherDigits; ++i) {
        const uint8_t packed = bytes[2U + i / 2U];
        location.digits.push_back(static_cast<uint8_t>((i % 2U == 0) ? packed >> 4 : packed & 0x0F));
    }

    if (hasSubcodes) {
        location.subcodes = readBe16(bytes + 2U + digitBytes);
        if (*location.subcodes == 0 || *location.subcodes == 0xFFFF) {
            return std::nullopt;
        }
    }

    consumed = totalSize;
    return location;
}

std::optional<GeographicBounds> locationBounds(const LocationCode& location) {
    if (location.zone < 1 || location.zone > 40 || location.digits.empty()) {
        return std::nullopt;
    }

    const int zoneIndex = location.zone - 1;
    double southerlyMin = 18.0 + static_cast<double>(zoneIndex / 10) * 36.0;
    double easterlyMin = static_cast<double>(zoneIndex % 10) * 36.0;
    double height = 36.0;
    double width = 36.0;

    for (uint8_t digit : location.digits) {
        height /= 4.0;
        width /= 4.0;
        southerlyMin += static_cast<double>(digit >> 2) * height;
        easterlyMin += static_cast<double>(digit & 0x03) * width;
    }

    double west = easterlyMin;
    double east = easterlyMin + width;
    if (west >= 180.0) {
        west -= 360.0;
        east -= 360.0;
    }

    return GeographicBounds{
        90.0 - (southerlyMin + height),
        west,
        90.0 - southerlyMin,
        east
    };
}

std::string locationName(const LocationCode& location) {
    std::ostringstream output;
    output << 'Z' << static_cast<int>(location.zone) << ':' << std::hex << std::uppercase;
    for (uint8_t digit : location.digits) {
        output << static_cast<int>(digit);
    }
    if (location.subcodes.has_value()) {
        output << "/subcodes=" << std::setw(4) << std::setfill('0') << *location.subcodes;
    }
    return output.str();
}

const char* phaseName(uint8_t phase) {
    static constexpr std::array<const char*, 4> names = {"Pre-trigger", "Trigger", "Sustain", "End"};
    return names[phase & 0x03];
}

const char* stageName(uint8_t stage) {
    static constexpr std::array<const char*, 8> names = {
        "Level 1 Start", "Level 1 Update", "Level 1 Repeat", "Level 1 Critical",
        "Level 2 Start", "Level 2 Update", "Level 2 Repeat", "Test"
    };
    return names[stage & 0x07];
}

std::optional<EnsembleTime> decodeDabTime(const uint8_t* field, std::size_t length) {
    if (length < 5U || (field[0] & 0x1F) != 10 || (field[0] & 0xE0) != 0) {
        return std::nullopt;
    }
    // FIG 0/10's UTC flag selects long form, not UTC versus local time.
    const bool longForm = (field[3] & 0x08) != 0;
    if (longForm && length < 7U) {
        return std::nullopt;
    }
    const uint32_t modifiedJulianDate =
        (static_cast<uint32_t>(field[1] & 0x7F) << 10) |
        (static_cast<uint32_t>(field[2]) << 2) |
        (static_cast<uint32_t>(field[3]) >> 6);
    const int64_t serialDay = static_cast<int64_t>(modifiedJulianDate) - 40587;
    int64_t dayCount = serialDay + 719468;
    const int64_t era = (dayCount >= 0 ? dayCount : dayCount - 146096) / 146097;
    const uint32_t dayOfEra = static_cast<uint32_t>(dayCount - era * 146097);
    const uint32_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
    const uint32_t dayOfYear = dayOfEra - (365U * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const uint32_t monthPrime = (5U * dayOfYear + 2U) / 153U;

    EnsembleTime time;
    time.year = year + static_cast<int>(monthPrime < 10 ? 0 : 1);
    time.month = static_cast<int>(monthPrime) + (monthPrime < 10 ? 3 : -9);
    time.day = static_cast<int>(dayOfYear - (153U * monthPrime + 2U) / 5U + 1U);
    time.hasSeconds = longForm;
    time.hour = static_cast<uint8_t>(((field[3] & 0x07) << 2) | (field[4] >> 6));
    time.minute = static_cast<uint8_t>(field[4] & 0x3F);
    if (longForm) {
        time.second = static_cast<uint8_t>(field[5] >> 2);
        time.millisecond = static_cast<uint16_t>(((field[5] & 0x03) << 8) | field[6]);
    }
    if (time.hour > 23 || time.minute > 59 || time.second > 60 || time.millisecond > 999) {
        return std::nullopt;
    }
    return time;
}

std::optional<AsaMessage> decodeFig0Extension15(
        const uint8_t* field, std::size_t length, std::size_t frameIndex) {
    if (length < 1 || (field[0] & 0x1F) != 15) {
        return std::nullopt;
    }

    AsaMessage message;
    message.cn = (field[0] & 0x80) != 0;
    message.otherEnsemble = (field[0] & 0x40) != 0;
    message.pd = (field[0] & 0x20) != 0;
    message.frameIndex = frameIndex;

    if (length == 1) {
        message.heartbeat = message.cn && !message.otherEnsemble;
        return message;
    }

    std::size_t offset = 1;
    if (message.otherEnsemble) {
        if (offset + 2 > length) {
            return std::nullopt;
        }
        message.phase = 1;
        message.id = readBe16(field + offset);
        offset += 2;
    } else {
        const uint8_t id = field[offset++];
        message.phase = static_cast<uint8_t>(id >> 6);
        message.id = static_cast<uint16_t>(id & 0x3F);
        if (message.phase == 0) {
            if (offset >= length || (field[offset] & 0xC0) != 0) {
                return std::nullopt;
            }
            message.startSecond = static_cast<uint8_t>(field[offset++] & 0x3F);
        }
    }

    if (message.phase == 1 || message.phase == 0 || message.otherEnsemble) {
        if (offset >= length) {
            return std::nullopt;
        }
        const uint8_t status = field[offset++];
        message.last = (status & 0x80) != 0;
        message.stage = static_cast<uint8_t>((status >> 4) & 0x07);
        message.incidentId = static_cast<uint8_t>(status & 0x0F);

        while (offset < length) {
            std::size_t consumed = 0;
            auto location = decodeLocationCode(field + offset, length - offset, consumed);
            if (!location.has_value()) {
                return std::nullopt;
            }
            message.numFollowing = static_cast<uint8_t>(field[offset] >> 6);
            message.locations.push_back(std::move(*location));
            offset += consumed;
        }
    }

    return message;
}

void decodeFig0Extension2(
        const uint8_t* field, std::size_t length, std::vector<AudioService>& audioServices) {
    if (length < 1 || (field[0] & 0x1F) != 2) {
        return;
    }

    const bool longServiceId = (field[0] & 0x20) != 0;
    const std::size_t serviceIdSize = longServiceId ? 4U : 2U;
    std::size_t offset = 1;

    while (offset + serviceIdSize + 1U <= length) {
        const uint32_t serviceId = longServiceId
            ? readBe32(field + offset)
            : readBe16(field + offset);
        offset += serviceIdSize;
        const uint8_t numComponents = static_cast<uint8_t>(field[offset++] & 0x0F);

        if (offset + 2U * numComponents > length) {
            return;
        }
        for (uint8_t i = 0; i < numComponents; ++i) {
            const uint8_t componentType = field[offset];
            const uint8_t componentId = field[offset + 1U];
            offset += 2;
            if ((componentType >> 6) != 0) {
                continue;
            }

            const AudioService service{
                serviceId,
                static_cast<uint8_t>(componentId >> 2),
                static_cast<uint8_t>(componentType & 0x3F)
            };
            const auto duplicate = std::find_if(audioServices.begin(), audioServices.end(),
                [&service](const AudioService& existing) {
                    return existing.serviceId == service.serviceId &&
                           existing.subchannelId == service.subchannelId &&
                           existing.audioServiceComponentType == service.audioServiceComponentType;
                });
            if (duplicate == audioServices.end()) {
                audioServices.push_back(service);
            }
        }
    }
}

void decodeFib(const uint8_t* fib, std::size_t frameIndex,
    std::vector<AsaMessage>& messages, std::vector<AudioService>& audioServices,
    std::optional<EnsembleTime>& ensembleTime) {
    if (crcCcitt(fib, kFibDataSize) != readBe16(fib + kFibDataSize)) {
        return;
    }

    std::size_t offset = 0;
    while (offset < kFibDataSize) {
        const uint8_t header = fib[offset];
        const uint8_t type = static_cast<uint8_t>(header >> 5);
        const std::size_t length = header & 0x1F;
        if (type == 7) {
            break;
        }
        if (length == 0 || offset + 1U + length > kFibDataSize) {
            break;
        }

        if (type == 0) {
            const auto decodedTime = decodeDabTime(fib + offset + 1U, length);
            if (decodedTime.has_value()) {
                ensembleTime = decodedTime;
            }
            auto message = decodeFig0Extension15(fib + offset + 1U, length, frameIndex);
            if (message.has_value()) {
                message->ensembleTime = ensembleTime;
                messages.push_back(std::move(*message));
            }
            decodeFig0Extension2(fib + offset + 1U, length, audioServices);
        }
        offset += 1U + length;
    }
}

std::vector<AsaMessage> scanForAsa(const std::vector<uint8_t>& stream) {
    std::vector<AsaMessage> messages;
    std::vector<AudioService> audioServices;
    std::optional<EnsembleTime> ensembleTime;
    std::size_t frameIndex = 0;

    for (std::size_t offset = 0; offset + 12 <= stream.size();) {
        if (!hasEtiSync(stream.data() + offset)) {
            ++offset;
            continue;
        }

        const uint8_t numStreams = static_cast<uint8_t>(stream[offset + 5] & 0x7F);
        const bool ficPresent = (stream[offset + 5] & 0x80) != 0;
        const uint8_t modeIdentity = static_cast<uint8_t>((stream[offset + 6] >> 3) & 0x03);
        const std::size_t ficSize = modeIdentity == 3 ? 128U : 96U;
        const std::size_t ficOffset = offset + 12U + 4U * numStreams;

        if (ficPresent && numStreams <= 64 && ficOffset + ficSize <= stream.size()) {
            for (std::size_t fibOffset = 0; fibOffset < ficSize; fibOffset += kFibSize) {
                decodeFib(stream.data() + ficOffset + fibOffset, frameIndex, messages, audioServices, ensembleTime);
            }
        }

        ++frameIndex;
        offset += std::min(kEtiFrameSize, stream.size() - offset);
    }
    for (auto& message : messages) {
        if (message.otherEnsemble || message.heartbeat) {
            continue;
        }
        const auto service = std::find_if(audioServices.begin(), audioServices.end(),
            [&message](const AudioService& candidate) {
                return candidate.subchannelId == message.id;
            });
        if (service != audioServices.end()) {
            message.serviceId = service->serviceId;
            message.audioServiceComponentType = service->audioServiceComponentType;
        }
    }
    return messages;
}

const char* audioCodecName(uint8_t audioServiceComponentType) {
    if (audioServiceComponentType == 0) {
        return "DAB (MPEG Audio Layer II)";
    }
    if (audioServiceComponentType == 63) {
        return "DAB+ (HE-AAC v2)";
    }
    return "unknown audio codec";
}

std::string formatDabTime(const EnsembleTime& time) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << time.year << '-'
           << std::setw(2) << time.month << '-' << std::setw(2) << time.day << ' '
           << std::setw(2) << static_cast<int>(time.hour) << ':'
            << std::setw(2) << static_cast<int>(time.minute);
        if (time.hasSeconds) {
         output << ':' << std::setw(2) << static_cast<int>(time.second) << '.'
             << std::setw(3) << time.millisecond;
    }
        output << " UTC";
    return output.str();
}

void printMessage(const AsaMessage& message) {
    std::cout << "ASA " << (message.heartbeat ? "heartbeat" : "detected")
              << " at ETI frame " << message.frameIndex;
    if (message.ensembleTime.has_value()) {
        std::cout << " (DAB time " << formatDabTime(*message.ensembleTime) << ')';
    } else {
        std::cout << " (DAB time unavailable)";
    }
    std::cout << '\n';
    std::cout << "Scope: " << (message.otherEnsemble ? "other ensemble" : "tuned ensemble") << '\n';
    std::cout << "FIG flags: C/N=" << (message.cn ? 1 : 0)
              << ", OE=" << (message.otherEnsemble ? 1 : 0)
              << ", P/D=" << (message.pd ? 1 : 0) << '\n';

    if (message.heartbeat) {
        return;
    }

    std::cout << "Phase: " << phaseName(message.phase) << '\n';
    if (message.otherEnsemble) {
        std::cout << "Target ensemble ID: 0x" << std::hex << std::uppercase << std::setw(4)
                  << std::setfill('0') << message.id << std::dec << '\n';
    } else {
        std::cout << "Alert audio subchannel ID: " << message.id << '\n';
        if (message.serviceId.has_value()) {
            std::cout << "Audio service ID: 0x" << std::hex << std::uppercase
                      << *message.serviceId << std::dec;
            if (message.audioServiceComponentType.has_value()) {
                std::cout << ", codec: " << audioCodecName(*message.audioServiceComponentType)
                          << " (ASCTy " << static_cast<int>(*message.audioServiceComponentType) << ')';
            }
            std::cout << '\n';
        }
    }
    if (message.startSecond.has_value()) {
        std::cout << "Trigger starts at second: " << static_cast<int>(*message.startSecond) << '\n';
    }
    if (message.stage.has_value()) {
        std::cout << "Stage: " << stageName(*message.stage)
                  << ", incident ID: " << static_cast<int>(*message.incidentId)
                  << ", last in group: " << (message.last ? "yes" : "no") << '\n';
    }

    if (message.locations.empty()) {
        std::cout << "Alert area: complete ensemble coverage area\n";
        return;
    }

    std::cout << "Location codes (" << static_cast<int>(message.numFollowing)
              << " FIG instance(s) follow):\n";
    for (const auto& location : message.locations) {
        std::cout << "  " << locationName(location);
        const auto bounds = locationBounds(location);
        if (bounds.has_value()) {
            const double centreLatitude = (bounds->south + bounds->north) / 2.0;
            const double centreLongitude = (bounds->west + bounds->east) / 2.0;
            std::cout << std::fixed << std::setprecision(6)
                      << " bounds=[" << bounds->south << ", " << bounds->west
                      << "]..[" << bounds->north << ", " << bounds->east << ']'
                      << " centre=" << centreLatitude << ", " << centreLongitude;
        }
        std::cout << '\n';
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<uint8_t> stream;
    if (argc > 1) {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            std::cerr << "Unable to open input file: " << argv[1] << '\n';
            return 1;
        }
        stream = readAll(input);
    } else {
        stream = readAll(std::cin);
    }

    if (stream.empty()) {
        std::cerr << "No input data received. Pass an ETI(NI) file or pipe one to stdin.\n";
        return 1;
    }

    const auto messages = scanForAsa(stream);
    if (messages.empty()) {
        std::cout << "No ASA FIG 0/15 found in the input stream.\n";
        return 0;
    }

    const AsaMessage* previous = nullptr;
    for (const auto& message : messages) {
        const bool retransmission = previous != nullptr && sameMessage(*previous, message) &&
            message.frameIndex - previous->frameIndex <= 100U;
        if (!retransmission) {
            printMessage(message);
        }
        previous = &message;
    }
    return 0;
}
