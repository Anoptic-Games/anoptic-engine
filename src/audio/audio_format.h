/* SPDX-FileCopyrightText: 2023 Anoptic Game Engine Authors
 *
 * SPDX-License-Identifier: LGPL-3.0 */
/*  == Anoptic Game Engine v0.0000001 == */

// Semantic device formats and reflected foreign-API projections.
// C++26 reflection is the sole registry: adding an enumerator without every mapping fails compilation.

#ifndef ANO_AUDIO_FORMAT_H
#define ANO_AUDIO_FORMAT_H

#if !defined(__cplusplus) || __cplusplus < 202302L
#error "audio_format.h requires C++26"
#endif

#include <meta>
#include <stddef.h>
#include <stdint.h>

#include <anoptic_audio.h>

enum class AnoAudioNumericKind : uint8_t { floating_point, signed_integer };
enum class AnoAudioSampleType : uint8_t;
enum class AnoAudioChannelLayout : uint8_t;
enum class AnoAudioInterleave : uint8_t;

struct AnoAudioStorage final {
    uint8_t bytes;
    uint8_t bits;
    AnoAudioNumericKind numeric;
};

struct AnoAudioSampleMapping final {
    AnoAudioStorage storage;
    uint16_t waveFormatTag;
    uint32_t waveSubFormatData1;
    int32_t alsaFormat;
    uint32_t pipeWireFormat;
    uint32_t coreAudioFlags;
};

struct AnoAudioChannelProjection final {
    uint8_t count;
    uint32_t waveMask;
    uint32_t pipeWirePositions[2];
};

struct AnoAudioInterleaveProjection final {
    int32_t alsaAccess;
    uint32_t coreAudioFlags;
};

inline constexpr uint16_t ANO_AUDIO_WAVE_FORMAT_PCM = 0x0001u;
inline constexpr uint16_t ANO_AUDIO_WAVE_FORMAT_IEEE_FLOAT = 0x0003u;
inline constexpr uint16_t ANO_AUDIO_WAVE_FORMAT_EXTENSIBLE = 0xFFFEu;
inline constexpr uint32_t ANO_AUDIO_CORE_FORMAT_LINEAR_PCM = 0x6C70636Du; // 'lpcm'
inline constexpr uint32_t ANO_AUDIO_CORE_FLAG_FLOAT = 1u << 0;
inline constexpr uint32_t ANO_AUDIO_CORE_FLAG_SIGNED_INTEGER = 1u << 2;
inline constexpr uint32_t ANO_AUDIO_CORE_FLAG_PACKED = 1u << 3;
inline constexpr uint32_t ANO_AUDIO_CORE_FLAG_NONINTERLEAVED = 1u << 5;

inline constexpr size_t ANO_AUDIO_SAMPLE_TYPE_COUNT = 2;
inline constexpr size_t ANO_AUDIO_CHANNEL_LAYOUT_COUNT = 2;
inline constexpr size_t ANO_AUDIO_INTERLEAVE_COUNT = 2;
inline constexpr size_t ANO_AUDIO_BACKEND_COUNT = 6;

enum class AnoAudioSampleType : uint8_t {
    f32 [[=AnoAudioSampleMapping{
        {4, 32, AnoAudioNumericKind::floating_point},
        ANO_AUDIO_WAVE_FORMAT_IEEE_FLOAT, 0x00000003u, 14, 283u,
        ANO_AUDIO_CORE_FLAG_FLOAT | ANO_AUDIO_CORE_FLAG_PACKED}]],
    s16 [[=AnoAudioSampleMapping{
        {2, 16, AnoAudioNumericKind::signed_integer},
        ANO_AUDIO_WAVE_FORMAT_PCM, 0x00000001u, 2, 259u,
        ANO_AUDIO_CORE_FLAG_SIGNED_INTEGER | ANO_AUDIO_CORE_FLAG_PACKED}]],
};

enum class AnoAudioChannelLayout : uint8_t {
    mono [[=AnoAudioChannelProjection{1, 0x00000004u, {2u, 0u}}]],
    stereo [[=AnoAudioChannelProjection{2, 0x00000003u, {3u, 4u}}]],
};

enum class AnoAudioInterleave : uint8_t {
    interleaved [[=AnoAudioInterleaveProjection{3, 0u}]],
    planar [[=AnoAudioInterleaveProjection{4, ANO_AUDIO_CORE_FLAG_NONINTERLEAVED}]],
};

struct AnoAudioFormat final {
    AnoAudioSampleType sample;
    uint32_t sampleRate;
    AnoAudioChannelLayout layout;
    AnoAudioInterleave interleave;
};

enum class AnoAudioRatePolicy : uint8_t { exact, platform_converter };

struct AnoAudioBackendCapability final {
    uint8_t directSampleMask;
    uint8_t convertedSampleMask;
    uint8_t layoutMask;
    uint8_t interleaveMask;
    AnoAudioRatePolicy ratePolicy;
};

constexpr uint8_t ano_audio_bit(AnoAudioSampleType value)
{
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(value));
}

constexpr uint8_t ano_audio_bit(AnoAudioChannelLayout value)
{
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(value));
}

constexpr uint8_t ano_audio_bit(AnoAudioInterleave value)
{
    return static_cast<uint8_t>(1u << static_cast<uint8_t>(value));
}

enum class AnoAudioPlatform : uint8_t {
    null [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        0,
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::exact}]] = ANO_AUDIO_BACKEND_NULL_DEV,
    pipewire [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        0,
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::platform_converter}]] = ANO_AUDIO_BACKEND_PIPEWIRE,
    alsa [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        ano_audio_bit(AnoAudioSampleType::s16),
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::platform_converter}]] = ANO_AUDIO_BACKEND_ALSA,
    wasapi [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        0,
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::platform_converter}]] = ANO_AUDIO_BACKEND_WASAPI,
    dsound [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        ano_audio_bit(AnoAudioSampleType::s16),
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::platform_converter}]] = ANO_AUDIO_BACKEND_DSOUND,
    coreaudio [[=AnoAudioBackendCapability{
        ano_audio_bit(AnoAudioSampleType::f32),
        0,
        ano_audio_bit(AnoAudioChannelLayout::stereo),
        ano_audio_bit(AnoAudioInterleave::interleaved),
        AnoAudioRatePolicy::platform_converter}]] = ANO_AUDIO_BACKEND_COREAUDIO,
};

template<class Annotation>
consteval Annotation ano_audio_annotation(std::meta::info declaration)
{
    const auto annotations = std::define_static_array(
        std::meta::annotations_of_with_type(declaration, ^^Annotation));
    if (annotations.size() != 1)
        __builtin_abort();
    return std::meta::extract<Annotation>(annotations[0]);
}

template<class Mapping, size_t Count>
struct AnoAudioRegistry final {
    Mapping mappings[Count];
    const char* names[Count];
};

template<class Enum, class Mapping, size_t Count, size_t FirstValue>
consteval auto ano_reflect_audio_enum()
{
    static constexpr auto enumerators =
        std::define_static_array(std::meta::enumerators_of(^^Enum));
    static_assert(enumerators.size() == Count);
    AnoAudioRegistry<Mapping, Count> result{};
    bool seen[Count] = {};
    template for (constexpr auto enumerator : enumerators) {
        constexpr Enum value = [:enumerator:];
        constexpr size_t raw = static_cast<size_t>(value);
        static_assert(raw >= FirstValue && raw - FirstValue < Count);
        constexpr size_t index = raw - FirstValue;
        if (seen[index])
            __builtin_abort();
        seen[index] = true;
        result.mappings[index] = ano_audio_annotation<Mapping>(enumerator);
        result.names[index] = std::define_static_string(std::meta::identifier_of(enumerator));
    }
    for (bool present : seen)
        if (!present)
            __builtin_abort();
    return result;
}

consteval auto ano_reflect_audio_samples()
{
    auto result = ano_reflect_audio_enum<AnoAudioSampleType, AnoAudioSampleMapping,
        ANO_AUDIO_SAMPLE_TYPE_COUNT, 0>();
    for (const AnoAudioSampleMapping& mapping : result.mappings)
        if (mapping.storage.bytes == 0 ||
            mapping.storage.bytes * 8u != mapping.storage.bits)
            __builtin_abort();
    return result;
}

consteval auto ano_reflect_audio_channels()
{
    auto result = ano_reflect_audio_enum<AnoAudioChannelLayout, AnoAudioChannelProjection,
        ANO_AUDIO_CHANNEL_LAYOUT_COUNT, 0>();
    for (const AnoAudioChannelProjection& mapping : result.mappings)
        if (mapping.count == 0 || mapping.count > 2 || mapping.waveMask == 0)
            __builtin_abort();
    return result;
}

consteval auto ano_reflect_audio_interleaves()
{
    return ano_reflect_audio_enum<AnoAudioInterleave, AnoAudioInterleaveProjection,
        ANO_AUDIO_INTERLEAVE_COUNT, 0>();
}

consteval auto ano_reflect_audio_backends()
{
    auto result = ano_reflect_audio_enum<AnoAudioPlatform, AnoAudioBackendCapability,
        ANO_AUDIO_BACKEND_COUNT, 1>();
    for (const AnoAudioBackendCapability& capability : result.mappings)
        if ((capability.directSampleMask | capability.convertedSampleMask) == 0 ||
            (capability.directSampleMask & capability.convertedSampleMask) != 0 ||
            capability.layoutMask == 0 || capability.interleaveMask == 0)
            __builtin_abort();
    return result;
}

inline constexpr auto ANO_AUDIO_SAMPLE_REGISTRY = ano_reflect_audio_samples();
inline constexpr auto ANO_AUDIO_CHANNEL_REGISTRY = ano_reflect_audio_channels();
inline constexpr auto ANO_AUDIO_INTERLEAVE_REGISTRY = ano_reflect_audio_interleaves();
inline constexpr auto ANO_AUDIO_BACKEND_REGISTRY = ano_reflect_audio_backends();

constexpr const AnoAudioSampleMapping* ano_audio_sample_mapping(AnoAudioSampleType sample)
{
    const size_t index = static_cast<size_t>(sample);
    return index < ANO_AUDIO_SAMPLE_TYPE_COUNT ? &ANO_AUDIO_SAMPLE_REGISTRY.mappings[index] : nullptr;
}

constexpr const AnoAudioChannelProjection* ano_audio_channel_mapping(AnoAudioChannelLayout layout)
{
    const size_t index = static_cast<size_t>(layout);
    return index < ANO_AUDIO_CHANNEL_LAYOUT_COUNT ? &ANO_AUDIO_CHANNEL_REGISTRY.mappings[index] : nullptr;
}

constexpr const AnoAudioInterleaveProjection* ano_audio_interleave_mapping(
    AnoAudioInterleave interleave)
{
    const size_t index = static_cast<size_t>(interleave);
    return index < ANO_AUDIO_INTERLEAVE_COUNT
        ? &ANO_AUDIO_INTERLEAVE_REGISTRY.mappings[index] : nullptr;
}

constexpr const AnoAudioBackendCapability* ano_audio_backend_capability(AnoAudioBackend backend)
{
    const size_t value = static_cast<size_t>(backend);
    return value > 0 && value <= ANO_AUDIO_BACKEND_COUNT
        ? &ANO_AUDIO_BACKEND_REGISTRY.mappings[value - 1] : nullptr;
}

constexpr bool ano_audio_format_valid(AnoAudioFormat format)
{
    return format.sampleRate != 0 && ano_audio_sample_mapping(format.sample) &&
        ano_audio_channel_mapping(format.layout) && ano_audio_interleave_mapping(format.interleave);
}

constexpr AnoAudioFormat ano_audio_mix_format(uint32_t sampleRate)
{
    return {AnoAudioSampleType::f32, sampleRate,
            AnoAudioChannelLayout::stereo, AnoAudioInterleave::interleaved};
}

enum class AnoAudioFormatPath : uint8_t { unsupported, direct, engine_conversion };

constexpr AnoAudioFormatPath ano_audio_backend_format_path(
    AnoAudioBackend backend, AnoAudioFormat format)
{
    const AnoAudioBackendCapability* capability = ano_audio_backend_capability(backend);
    if (!capability || !ano_audio_format_valid(format) ||
        (capability->layoutMask & ano_audio_bit(format.layout)) == 0 ||
        (capability->interleaveMask & ano_audio_bit(format.interleave)) == 0)
        return AnoAudioFormatPath::unsupported;
    const uint8_t sample = ano_audio_bit(format.sample);
    if ((capability->directSampleMask & sample) != 0)
        return AnoAudioFormatPath::direct;
    return (capability->convertedSampleMask & sample) != 0
        ? AnoAudioFormatPath::engine_conversion : AnoAudioFormatPath::unsupported;
}

constexpr bool ano_audio_backend_supports(AnoAudioBackend backend, AnoAudioFormat format)
{
    return ano_audio_backend_format_path(backend, format) != AnoAudioFormatPath::unsupported;
}

constexpr const char* ano_audio_sample_name(AnoAudioSampleType sample)
{
    const size_t index = static_cast<size_t>(sample);
    return index < ANO_AUDIO_SAMPLE_TYPE_COUNT ? ANO_AUDIO_SAMPLE_REGISTRY.names[index] : "invalid";
}

constexpr const char* ano_audio_layout_name(AnoAudioChannelLayout layout)
{
    const size_t index = static_cast<size_t>(layout);
    return index < ANO_AUDIO_CHANNEL_LAYOUT_COUNT ? ANO_AUDIO_CHANNEL_REGISTRY.names[index] : "invalid";
}

constexpr const char* ano_audio_interleave_name(AnoAudioInterleave interleave)
{
    const size_t index = static_cast<size_t>(interleave);
    return index < ANO_AUDIO_INTERLEAVE_COUNT ? ANO_AUDIO_INTERLEAVE_REGISTRY.names[index] : "invalid";
}

constexpr const char* ano_audio_backend_name(AnoAudioBackend backend)
{
    const size_t value = static_cast<size_t>(backend);
    return value > 0 && value <= ANO_AUDIO_BACKEND_COUNT
        ? ANO_AUDIO_BACKEND_REGISTRY.names[value - 1] : "invalid";
}

constexpr uint32_t ano_audio_frame_bytes(AnoAudioFormat format)
{
    const AnoAudioSampleMapping* sample = ano_audio_sample_mapping(format.sample);
    const AnoAudioChannelProjection* channels = ano_audio_channel_mapping(format.layout);
    return sample && channels ? sample->storage.bytes * channels->count : 0u;
}

enum class AnoAudioWaveEncoding : uint8_t { classic, extensible };

struct AnoAudioWaveProjection final {
    bool valid;
    uint16_t formatTag;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t averageBytesPerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t extraSize;
    uint16_t validBitsPerSample;
    uint32_t channelMask;
    uint32_t subFormatData1;
};

constexpr AnoAudioWaveProjection ano_audio_project_wave(
    AnoAudioFormat format, AnoAudioWaveEncoding encoding)
{
    AnoAudioWaveProjection result{};
    const AnoAudioSampleMapping* sample = ano_audio_sample_mapping(format.sample);
    const AnoAudioChannelProjection* channels = ano_audio_channel_mapping(format.layout);
    const uint32_t frameBytes = ano_audio_frame_bytes(format);
    if (!ano_audio_format_valid(format) || format.interleave != AnoAudioInterleave::interleaved ||
        !sample || !channels || frameBytes > UINT16_MAX ||
        format.sampleRate > UINT32_MAX / frameBytes)
        return result;
    result.valid = true;
    result.formatTag = encoding == AnoAudioWaveEncoding::extensible
        ? ANO_AUDIO_WAVE_FORMAT_EXTENSIBLE : sample->waveFormatTag;
    result.channels = channels->count;
    result.sampleRate = format.sampleRate;
    result.averageBytesPerSecond = format.sampleRate * frameBytes;
    result.blockAlign = static_cast<uint16_t>(frameBytes);
    result.bitsPerSample = sample->storage.bits;
    result.extraSize = encoding == AnoAudioWaveEncoding::extensible ? 22u : 0u;
    result.validBitsPerSample = sample->storage.bits;
    result.channelMask = channels->waveMask;
    result.subFormatData1 = sample->waveSubFormatData1;
    return result;
}

struct AnoAudioAlsaProjection final {
    bool valid;
    int32_t format;
    int32_t access;
    uint32_t channels;
    uint32_t sampleRate;
};

constexpr AnoAudioAlsaProjection ano_audio_project_alsa(AnoAudioFormat format)
{
    const AnoAudioSampleMapping* sample = ano_audio_sample_mapping(format.sample);
    const AnoAudioChannelProjection* channels = ano_audio_channel_mapping(format.layout);
    const AnoAudioInterleaveProjection* interleave = ano_audio_interleave_mapping(format.interleave);
    return sample && channels && interleave && format.sampleRate != 0
        ? AnoAudioAlsaProjection{true, sample->alsaFormat, interleave->alsaAccess,
                                 channels->count, format.sampleRate}
        : AnoAudioAlsaProjection{};
}

struct AnoAudioPipeWireProjection final {
    bool valid;
    uint32_t format;
    uint32_t channels;
    uint32_t sampleRate;
    uint32_t positions[2];
};

constexpr AnoAudioPipeWireProjection ano_audio_project_pipewire(AnoAudioFormat format)
{
    AnoAudioPipeWireProjection result{};
    const AnoAudioSampleMapping* sample = ano_audio_sample_mapping(format.sample);
    const AnoAudioChannelProjection* channels = ano_audio_channel_mapping(format.layout);
    if (!sample || !channels || format.sampleRate == 0 ||
        format.interleave != AnoAudioInterleave::interleaved)
        return result;
    result.valid = true;
    result.format = sample->pipeWireFormat;
    result.channels = channels->count;
    result.sampleRate = format.sampleRate;
    result.positions[0] = channels->pipeWirePositions[0];
    result.positions[1] = channels->pipeWirePositions[1];
    return result;
}

struct AnoAudioCoreAudioProjection final {
    bool valid;
    uint32_t formatId;
    uint32_t formatFlags;
    uint32_t bytesPerPacket;
    uint32_t framesPerPacket;
    uint32_t bytesPerFrame;
    uint32_t channelsPerFrame;
    uint32_t bitsPerChannel;
};

constexpr AnoAudioCoreAudioProjection ano_audio_project_coreaudio(AnoAudioFormat format)
{
    AnoAudioCoreAudioProjection result{};
    const AnoAudioSampleMapping* sample = ano_audio_sample_mapping(format.sample);
    const AnoAudioChannelProjection* channels = ano_audio_channel_mapping(format.layout);
    const AnoAudioInterleaveProjection* interleave = ano_audio_interleave_mapping(format.interleave);
    const uint32_t interleavedFrameBytes = ano_audio_frame_bytes(format);
    if (!sample || !channels || !interleave || format.sampleRate == 0 ||
        interleavedFrameBytes == 0)
        return result;
    const uint32_t bufferFrameBytes = format.interleave == AnoAudioInterleave::interleaved
        ? interleavedFrameBytes : sample->storage.bytes;
    result.valid = true;
    result.formatId = ANO_AUDIO_CORE_FORMAT_LINEAR_PCM;
    result.formatFlags = sample->coreAudioFlags | interleave->coreAudioFlags;
    result.bytesPerPacket = bufferFrameBytes;
    result.framesPerPacket = 1;
    result.bytesPerFrame = bufferFrameBytes;
    result.channelsPerFrame = channels->count;
    result.bitsPerChannel = sample->storage.bits;
    return result;
}

static_assert(ANO_AUDIO_CHANNEL_REGISTRY.mappings[static_cast<size_t>(
                  AnoAudioChannelLayout::stereo)].count == ANO_AUDIO_CHANNELS);
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_NULL_DEV, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_PIPEWIRE, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_ALSA, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_WASAPI, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_DSOUND, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_supports(ANO_AUDIO_BACKEND_COREAUDIO, ano_audio_mix_format(48000)));
static_assert(ano_audio_backend_format_path(
                  ANO_AUDIO_BACKEND_ALSA,
                  {AnoAudioSampleType::s16, 48000, AnoAudioChannelLayout::stereo,
                   AnoAudioInterleave::interleaved}) == AnoAudioFormatPath::engine_conversion);
static_assert(ano_audio_backend_format_path(
                  ANO_AUDIO_BACKEND_DSOUND,
                  {AnoAudioSampleType::s16, 48000, AnoAudioChannelLayout::stereo,
                   AnoAudioInterleave::interleaved}) == AnoAudioFormatPath::engine_conversion);

#endif // ANO_AUDIO_FORMAT_H
