#include "PlaybackInfo.h"

#include <QFile>
#include <QScreen>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QWidget>
#include <QWindow>

#include <cmath>
#include <optional>
#include <tuple>

#include "ContextMenu.h"
#include "TimeFormat.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <dxgi1_6.h>
#include <psapi.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace {

constexpr double kSdrReferenceNits = 203.0;  // mpv reports HDR peaks relative to this.

// Collects rows under one heading, leaving out anything with no value.
class Rows {
public:
    explicit Rows(std::vector<InfoRow>& rows) : rows_(rows) {}
    void Group(const QString& group) { group_ = group; }
    void Add(const QString& label, const QString& value) {
        if (!value.isEmpty()) {
            rows_.push_back({group_, label, value});
        }
    }

private:
    std::vector<InfoRow>& rows_;
    QString group_;
};

QString Number(double value, int decimals) {
    return QString::number(value, 'f', decimals);
}

QString Whole(double value) {
    return QString::number(std::llround(value));
}

QString Bitrate(double bitsPerSecond) {
    if (bitsPerSecond >= 1'000'000.0) {
        return Number(bitsPerSecond / 1'000'000.0, 2) + QStringLiteral(" Mbps");
    }
    return Whole(bitsPerSecond / 1000.0) + QStringLiteral(" kbps");
}

QString Bitrate(const std::optional<double>& bitsPerSecond) {
    return bitsPerSecond && *bitsPerSecond > 0.0 ? Bitrate(*bitsPerSecond) : QString();
}

QString Bytes(double bytes) {
    constexpr double kKilo = 1024.0;
    if (bytes >= kKilo * kKilo * kKilo) {
        return Number(bytes / (kKilo * kKilo * kKilo), 2) + QStringLiteral(" GB");
    }
    if (bytes >= kKilo * kKilo) {
        return Number(bytes / (kKilo * kKilo), 1) + QStringLiteral(" MB");
    }
    return Whole(bytes / kKilo) + QStringLiteral(" KB");
}

QString Size(const std::optional<double>& width, const std::optional<double>& height) {
    return width && height && *width > 0.0 ? Whole(*width) + QStringLiteral(" x ") + Whole(*height) : QString();
}

QString Fps(const std::optional<double>& fps) {
    return fps && *fps > 0.0 ? Number(*fps, 3) + QStringLiteral(" fps") : QString();
}

QString Seconds(double seconds, int decimals) {
    return QString::asprintf("%+.*f초", decimals, seconds);
}

QString HdrDescription(const QString& transfer, const QString& matrix) {
    if (transfer.isEmpty()) {
        return {};
    }
    if (matrix == QLatin1String("dolbyvision")) {
        return QStringLiteral("예 (Dolby Vision)");
    }
    return transfer == QLatin1String("pq")  ? QStringLiteral("예 (PQ)")
         : transfer == QLatin1String("hlg") ? QStringLiteral("예 (HLG)")
                                            : QStringLiteral("아니요");
}

QString ChannelConversion(const QString& source, const QString& output, double sourceCount, double outputCount) {
    if (outputCount == sourceCount) {
        return source == output ? QStringLiteral("없음")
                                : QStringLiteral("배치 변환 (%1 → %2)").arg(source, output);
    }
    return (outputCount < sourceCount ? QStringLiteral("다운믹스 (") : QStringLiteral("업믹스 (")) +
           source + QStringLiteral(" → ") + output + QLatin1Char(')');
}

const MediaTrack* SelectedTrack(const PlaybackState& state, TrackType type) {
    for (const MediaTrack& track : state.tracks) {
        if (track.type == type && track.selected) {
            return &track;
        }
    }
    return nullptr;
}

QString Join(const QStringList& parts) {
    QStringList nonEmpty;
    for (const QString& part : parts) {
        if (!part.isEmpty()) {
            nonEmpty.push_back(part);
        }
    }
    return nonEmpty.join(QStringLiteral(", "));
}

QString TrackDescription(const MediaTrack& track) {
    QStringList parts{
        track.codecDescription.isEmpty() ? track.codec : track.codecDescription,
        track.codecProfile,
    };
    if (track.type == TrackType::Other || track.width > 0) {
        parts.push_back(track.width > 0 ? QStringLiteral("%1 x %2").arg(track.width).arg(track.height) : QString());
        parts.push_back(track.fps > 0.0 ? Number(track.fps, 3) + QStringLiteral(" fps") : QString());
    }
    parts.push_back(track.channelLayout);
    parts.push_back(track.sampleRate > 0 ? Number(track.sampleRate / 1000.0, 1) + QStringLiteral(" kHz") : QString());
    parts.push_back(track.bitrate > 0 ? Bitrate(static_cast<double>(track.bitrate)) : QString());
    parts.push_back(track.language);
    parts.push_back(track.title);
    parts.push_back(track.isDefault ? QStringLiteral("기본") : QString());
    parts.push_back(track.forced ? QStringLiteral("강제") : QString());
    parts.push_back(track.externalFileName.isEmpty() ? QString() : QStringLiteral("외부 파일 ") + track.externalFileName);
    parts.push_back(track.selected ? QStringLiteral("사용 중") : QString());
    return Join(parts);
}

// libass packs its version as hex digits: 0x01704000 is 0.17.4.
QString LibassVersion(const std::optional<double>& packed) {
    if (!packed) {
        return {};
    }
    const unsigned value = static_cast<unsigned>(*packed);
    return QString::asprintf("%x.%x.%x", value >> 28, (value >> 20) & 0xFF, (value >> 12) & 0xFF);
}

void AddScreenFallbackRows(Rows& rows, QWidget* window) {
    QScreen* screen = window && window->windowHandle() ? window->windowHandle()->screen()
                    : window                           ? window->screen()
                                                       : nullptr;
    if (!screen) {
        return;
    }
    const QSize physSize = screen->size() * screen->devicePixelRatio();
    rows.Add(QStringLiteral("해상도"), QStringLiteral("%1 x %2").arg(physSize.width()).arg(physSize.height()));
    if (screen->refreshRate() > 0.0) {
        rows.Add(QStringLiteral("주사율"), Whole(screen->refreshRate()) + QStringLiteral(" Hz"));
    }
    rows.Add(QStringLiteral("화면 배율"), Whole(screen->devicePixelRatio() * 100.0) + QLatin1Char('%'));
    if (screen->depth() >= 24) {
        rows.Add(QStringLiteral("색 깊이"), QString::number(screen->depth() / 3) + QStringLiteral("비트"));
    }
}

#if defined(Q_OS_WIN)

QString RegistryText(const wchar_t* key, const wchar_t* name) {
    wchar_t buffer[256];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(buffer);
}

QString WindowsVersion() {
    using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion = reinterpret_cast<RtlGetVersionFunction>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW version{sizeof(version)};
    if (!getVersion || getVersion(&version) != 0) {
        return QSysInfo::prettyProductName();
    }
    constexpr wchar_t kKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    QString text = version.dwBuildNumber >= 22000 ? QStringLiteral("Windows 11")
                                                  : QStringLiteral("Windows ") + QString::number(version.dwMajorVersion);
    const QString release = RegistryText(kKey, L"DisplayVersion");
    if (!release.isEmpty()) {
        text += QLatin1Char(' ') + release;
    }
    return text + QStringLiteral(" (빌드 ") + QString::number(version.dwBuildNumber) + QLatin1Char(')');
}

QString DriverVersion(IDXGIAdapter1& adapter) {
    LARGE_INTEGER version{};
    if (FAILED(adapter.CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
        return {};
    }
    const auto part = [&](int shift) { return QString::number((version.QuadPart >> shift) & 0xFFFF); };
    return part(48) + QLatin1Char('.') + part(32) + QLatin1Char('.') + part(16) + QLatin1Char('.') + part(0);
}

void AddMonitorRows(Rows& rows, IDXGIOutput& output, HWND hwnd) {
    ComPtr<IDXGIOutput6> details;
    DXGI_OUTPUT_DESC1 description{};
    if (FAILED(output.QueryInterface(IID_PPV_ARGS(&details))) || FAILED(details->GetDesc1(&description))) {
        return;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(description.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) {
        rows.Add(QStringLiteral("해상도"), QStringLiteral("%1 x %2").arg(mode.dmPelsWidth).arg(mode.dmPelsHeight));
        rows.Add(QStringLiteral("주사율"), QString::number(mode.dmDisplayFrequency) + QStringLiteral(" Hz"));
    }
    if (hwnd) {
        rows.Add(QStringLiteral("화면 배율"), QString::number(MulDiv(static_cast<int>(GetDpiForWindow(hwnd)), 100, 96)) + QLatin1Char('%'));
    }
    const bool hdr = description.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    rows.Add(QStringLiteral("HDR"), hdr ? QStringLiteral("켜짐") : QStringLiteral("꺼짐"));
    rows.Add(QStringLiteral("색 깊이"), QString::number(description.BitsPerColor) + QStringLiteral("비트"));
    if (description.MaxLuminance > 0.0f) {
        rows.Add(QStringLiteral("최대 밝기"), Whole(description.MaxLuminance) + QStringLiteral(" nit"));
    }
}

void AddGraphicsRows(Rows& rows, QWidget* window) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        rows.Group(QStringLiteral("현재 모니터"));
        AddScreenFallbackRows(rows, window);
        return;
    }
    const HWND hwnd = window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    const HMONITOR monitor = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
    ComPtr<IDXGIOutput> monitorOutput;
    std::vector<ComPtr<IDXGIAdapter1>> adapters;
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        adapters.push_back(adapter);
        ComPtr<IDXGIOutput> output;
        for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
            DXGI_OUTPUT_DESC outputDescription{};
            if (SUCCEEDED(output->GetDesc(&outputDescription)) && outputDescription.Monitor == monitor) {
                monitorOutput = output;
            }
        }
    }

    rows.Group(QStringLiteral("그래픽"));
    for (size_t i = 0; i < adapters.size(); ++i) {
        DXGI_ADAPTER_DESC1 description{};
        adapters[i]->GetDesc1(&description);
        const QString suffix = adapters.size() > 1 ? QLatin1Char(' ') + QString::number(i + 1) : QString();
        rows.Add(QStringLiteral("그래픽 카드") + suffix, QString::fromWCharArray(description.Description));
        if (description.DedicatedVideoMemory > 0) {
            rows.Add(QStringLiteral("전용 메모리") + suffix, Bytes(static_cast<double>(description.DedicatedVideoMemory)));
        }
        rows.Add(QStringLiteral("드라이버") + suffix, DriverVersion(*adapters[i].Get()));
    }

    rows.Group(QStringLiteral("현재 모니터"));
    if (monitorOutput) {
        AddMonitorRows(rows, *monitorOutput.Get(), hwnd);
    } else {
        AddScreenFallbackRows(rows, window);
    }
}

#endif

}  // namespace

std::vector<InfoRow> BuildPlaybackRows(const MpvPlayer& player, const PlaybackState& state) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(QStringLiteral("재생"));
    rows.Add(QStringLiteral("상태"),
             state.fileName.isEmpty() ? QString()
             : state.atEnd            ? QStringLiteral("끝남")
             : state.paused           ? QStringLiteral("일시정지")
                                      : QStringLiteral("재생 중"));
    if (state.position) {
        QString position = FormatTime(*state.position);
        if (state.duration && *state.duration > 0.0) {
            position += QStringLiteral(" / ") + FormatTime(*state.duration) +
                        QStringLiteral(" (") + Whole(*state.position / *state.duration * 100.0) + QStringLiteral("%)");
        }
        rows.Add(QStringLiteral("위치"), position);
    }
    rows.Add(QStringLiteral("배속"), QString::asprintf("%g배", state.speed));
    rows.Add(QStringLiteral("볼륨"), Whole(state.volume) + QLatin1Char('%'));
    if (state.chapter && *state.chapter >= 0 && static_cast<size_t>(*state.chapter) < state.chapters.size()) {
        const Chapter& chapter = state.chapters[static_cast<size_t>(*state.chapter)];
        rows.Add(QStringLiteral("챕터"),
                 QStringLiteral("%1 / %2: %3").arg(*state.chapter + 1).arg(state.chapters.size()).arg(chapter.title));
    }
    if (state.loopStart || state.loopEnd) {
        rows.Add(QStringLiteral("구간 반복"),
                 (state.loopStart ? FormatTime(*state.loopStart) : QStringLiteral("처음")) +
                 QStringLiteral(" ~ ") +
                 (state.loopEnd ? FormatTime(*state.loopEnd) : QStringLiteral("끝")));
    }
    if (state.aspect != AspectMode::Original) {
        rows.Add(QStringLiteral("화면 비율"), AspectLabel(state.aspect));
    }
    if (state.rotation != 0) {
        rows.Add(QStringLiteral("회전"), QString::number(state.rotation) + QStringLiteral("도"));
    }

    rows.Group(QStringLiteral("비디오"));
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Other)) {
        rows.Add(QStringLiteral("코덱"), Join({track->codecDescription.isEmpty() ? track->codec : track->codecDescription, track->codecProfile}));
    }
    const QString hardware = player.PropertyText("hwdec-current");
    if (!player.PropertyText("video-format").isEmpty()) {
        rows.Add(QStringLiteral("디코더"),
                 hardware.isEmpty() || hardware == QLatin1String("no") ? QStringLiteral("소프트웨어")
                                                                       : QStringLiteral("하드웨어 (") + hardware + QLatin1Char(')'));
    }
    rows.Add(QStringLiteral("원본 크기"), Size(player.PropertyNumber("video-params/w"), player.PropertyNumber("video-params/h")));
    rows.Add(QStringLiteral("화면에 맞춘 크기"), Size(player.PropertyNumber("osd-dimensions/w"), player.PropertyNumber("osd-dimensions/h")));
    const QString hardwareFormat = player.PropertyText("video-params/hw-pixelformat");
    rows.Add(QStringLiteral("픽셀 형식"), hardwareFormat.isEmpty() ? player.PropertyText("video-params/pixelformat") : hardwareFormat);
    const QString matrix = player.PropertyText("video-params/colormatrix");
    rows.Add(QStringLiteral("HDR"), HdrDescription(player.PropertyText("video-params/gamma"), matrix));
    rows.Add(QStringLiteral("행렬"), matrix);
    rows.Add(QStringLiteral("색 영역"), player.PropertyText("video-params/primaries"));
    const QString transfer = player.PropertyText("video-params/gamma");
    rows.Add(QStringLiteral("전달 함수"), transfer);
    if (transfer == QLatin1String("pq") || transfer == QLatin1String("hlg")) {
        const std::optional<double> peak = player.PropertyNumber("video-params/sig-peak");
        rows.Add(QStringLiteral("HDR 최대 밝기"), peak && *peak > 0.0 ? Whole(*peak * kSdrReferenceNits) + QStringLiteral(" nit") : QString());
    }
    rows.Add(QStringLiteral("파일의 프레임 속도"), Fps(player.PropertyNumber("container-fps")));
    rows.Add(QStringLiteral("출력 프레임 속도"), Fps(player.PropertyNumber("estimated-vf-fps")));
    rows.Add(QStringLiteral("모니터 주사율"), Fps(player.PropertyNumber("display-fps")));
    rows.Add(QStringLiteral("비트레이트"), Bitrate(player.PropertyNumber("video-bitrate")));
    const auto count = [&](const char* name) {
        const std::optional<double> value = player.PropertyNumber(name);
        return value ? Whole(*value) : QString();
    };
    rows.Add(QStringLiteral("버린 프레임 (출력)"), count("frame-drop-count"));
    rows.Add(QStringLiteral("버린 프레임 (디코더)"), count("decoder-frame-drop-count"));
    rows.Add(QStringLiteral("늦은 프레임"), count("vo-delayed-frame-count"));

    rows.Group(QStringLiteral("오디오"));
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Audio)) {
        rows.Add(QStringLiteral("코덱"), track->codecDescription.isEmpty() ? track->codec : track->codecDescription);
        rows.Add(QStringLiteral("언어"), track->language);
    }
    const std::optional<double> sampleRate = player.PropertyNumber("audio-params/samplerate");
    rows.Add(QStringLiteral("샘플레이트"), sampleRate ? Number(*sampleRate / 1000.0, 1) + QStringLiteral(" kHz") : QString());
    const std::optional<double> outputRate = player.PropertyNumber("audio-out-params/samplerate");
    if (outputRate && sampleRate && *outputRate != *sampleRate) {
        rows.Add(QStringLiteral("출력 샘플레이트"), Number(*outputRate / 1000.0, 1) + QStringLiteral(" kHz"));
    }
    const QString sourceChannels = player.PropertyText("audio-params/hr-channels");
    const QString outputChannels = player.PropertyText("audio-out-params/hr-channels");
    rows.Add(QStringLiteral("원본 채널"), sourceChannels);
    rows.Add(QStringLiteral("출력 채널"), outputChannels);
    const std::optional<double> sourceCount = player.PropertyNumber("audio-params/channel-count");
    const std::optional<double> outputCount = player.PropertyNumber("audio-out-params/channel-count");
    if (sourceCount && outputCount) {
        rows.Add(QStringLiteral("채널 변환"), ChannelConversion(sourceChannels, outputChannels, *sourceCount, *outputCount));
    }
    rows.Add(QStringLiteral("샘플 형식"), player.PropertyText("audio-params/format"));
    rows.Add(QStringLiteral("비트레이트"), Bitrate(player.PropertyNumber("audio-bitrate")));
    rows.Add(QStringLiteral("출력"), player.PropertyText("current-ao"));
    const std::optional<double> avSync = player.PropertyNumber("avsync");
    rows.Add(QStringLiteral("영상과의 차이"), avSync ? Number(*avSync * 1000.0, 1) + QStringLiteral(" ms") : QString());
    if (state.audioDelay != 0.0) {
        rows.Add(QStringLiteral("음성 싱크"), Seconds(state.audioDelay, 1));
    }

    rows.Group(QStringLiteral("자막"));
    if (const MediaTrack* track = SelectedTrack(state, TrackType::Subtitle)) {
        rows.Add(QStringLiteral("트랙"), TrackLabel(*track));
    } else if (!state.fileName.isEmpty()) {
        rows.Add(QStringLiteral("트랙"), QStringLiteral("없음"));
    }
    if (state.subtitleDelay != 0.0) {
        rows.Add(QStringLiteral("싱크"), Seconds(state.subtitleDelay, 1));
    }

    rows.Group(QStringLiteral("버퍼"));
    const std::optional<double> cached = player.PropertyNumber("demuxer-cache-duration");
    rows.Add(QStringLiteral("미리 읽은 시간"), cached ? Number(*cached, 1) + QStringLiteral("초") : QString());
    const std::optional<double> readSpeed = player.PropertyNumber("cache-speed");
    rows.Add(QStringLiteral("읽기 속도"), readSpeed ? Bytes(*readSpeed) + QStringLiteral("/s") : QString());
    return list;
}

std::vector<InfoRow> BuildFileRows(const MpvPlayer& player, const PlaybackState& state) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(QStringLiteral("파일"));
    rows.Add(QStringLiteral("이름"), state.fileName);
    rows.Add(QStringLiteral("경로"), player.PropertyText("path"));
    const std::optional<double> size = player.PropertyNumber("file-size");
    rows.Add(QStringLiteral("크기"), size ? Bytes(*size) : QString());
    rows.Add(QStringLiteral("형식"), player.PropertyText("file-format"));
    if (state.duration && *state.duration > 0.0) {
        rows.Add(QStringLiteral("길이"), FormatTime(*state.duration));
        rows.Add(QStringLiteral("평균 비트레이트"), size ? Bitrate(*size * 8.0 / *state.duration) : QString());
    }
    rows.Add(QStringLiteral("챕터"), state.chapters.empty() ? QString() : QString::number(state.chapters.size()) + QStringLiteral("개"));

    rows.Group(QStringLiteral("메타데이터"));
    for (const auto& [key, value] : state.metadata) {
        rows.Add(key, value);
    }

    for (const auto& [type, group, label] : {
             std::tuple{TrackType::Other, QStringLiteral("비디오 트랙"), QStringLiteral("비디오 ")},
             std::tuple{TrackType::Audio, QStringLiteral("오디오 트랙"), QStringLiteral("오디오 ")},
             std::tuple{TrackType::Subtitle, QStringLiteral("자막 트랙"), QStringLiteral("자막 ")},
         }) {
        rows.Group(group);
        for (const MediaTrack& track : state.tracks) {
            if (track.type == type) {
                rows.Add(label + QString::number(track.id), TrackDescription(track));
            }
        }
    }
    return list;
}

std::vector<InfoRow> BuildSystemRows(const MpvPlayer& player, QWidget* window) {
    std::vector<InfoRow> list;
    Rows rows(list);

    rows.Group(QStringLiteral("플레이어"));
    rows.Add(QStringLiteral("mpv"), player.PropertyText("mpv-version"));
    rows.Add(QStringLiteral("FFmpeg"), player.PropertyText("ffmpeg-version"));
    rows.Add(QStringLiteral("libass"), LibassVersion(player.PropertyNumber("libass-version")));
    rows.Add(QStringLiteral("영상 출력"), player.PropertyText("current-vo"));
    rows.Add(QStringLiteral("그래픽 API"), player.PropertyText("current-gpu-context"));

#if defined(Q_OS_WIN)
    PROCESS_MEMORY_COUNTERS memory{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        rows.Add(QStringLiteral("사용 중인 메모리"), Bytes(static_cast<double>(memory.WorkingSetSize)));
    }

    AddGraphicsRows(rows, window);

    rows.Group(QStringLiteral("시스템"));
    rows.Add(QStringLiteral("운영체제"), WindowsVersion());
    rows.Add(QStringLiteral("프로세서"), RegistryText(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString"));
    rows.Add(QStringLiteral("논리 프로세서"), QString::number(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)) + QStringLiteral("개"));
    MEMORYSTATUSEX systemMemory{sizeof(systemMemory)};
    if (GlobalMemoryStatusEx(&systemMemory)) {
        const double total = static_cast<double>(systemMemory.ullTotalPhys);
        const double available = static_cast<double>(systemMemory.ullAvailPhys);
        rows.Add(QStringLiteral("메모리"), Bytes(total) + QStringLiteral(" (사용 가능 ") + Bytes(available) + QLatin1Char(')'));
    }
#elif defined(Q_OS_LINUX)
    {
        QFile statm(QStringLiteral("/proc/self/statm"));
        if (statm.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QList<QByteArray> fields = statm.readLine().simplified().split(' ');
            if (fields.size() >= 2) {
                const double rssBytes = fields[1].toLongLong() * static_cast<double>(sysconf(_SC_PAGESIZE));
                if (rssBytes > 0.0) {
                    rows.Add(QStringLiteral("사용 중인 메모리"), Bytes(rssBytes));
                }
            }
        }
    }
    rows.Group(QStringLiteral("현재 모니터"));
    AddScreenFallbackRows(rows, window);

    rows.Group(QStringLiteral("시스템"));
    rows.Add(QStringLiteral("운영체제"), QSysInfo::prettyProductName());
    {
        QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
        if (cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!cpuinfo.atEnd()) {
                const QString line = QString::fromUtf8(cpuinfo.readLine());
                if (line.startsWith(QLatin1String("model name"))) {
                    const qsizetype colon = line.indexOf(QLatin1Char(':'));
                    if (colon >= 0) {
                        rows.Add(QStringLiteral("프로세서"), line.mid(colon + 1).trimmed());
                        break;
                    }
                }
            }
        }
    }
    rows.Add(QStringLiteral("논리 프로세서"), QString::number(QThread::idealThreadCount()) + QStringLiteral("개"));
    {
        QFile meminfo(QStringLiteral("/proc/meminfo"));
        if (meminfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
            double totalKb = 0.0;
            double availKb = 0.0;
            while (!meminfo.atEnd()) {
                const QString line = QString::fromUtf8(meminfo.readLine());
                if (line.startsWith(QLatin1String("MemTotal:"))) {
                    totalKb = line.section(QLatin1Char(' '), -2, -2, QString::SectionSkipEmpty).toDouble();
                } else if (line.startsWith(QLatin1String("MemAvailable:"))) {
                    availKb = line.section(QLatin1Char(' '), -2, -2, QString::SectionSkipEmpty).toDouble();
                }
            }
            if (totalKb > 0.0) {
                rows.Add(QStringLiteral("메모리"),
                         Bytes(totalKb * 1024.0) + QStringLiteral(" (사용 가능 ") + Bytes(availKb * 1024.0) + QLatin1Char(')'));
            }
        }
    }
#elif defined(Q_OS_MACOS)
    {
        mach_task_basic_info_data_t info{};
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
            rows.Add(QStringLiteral("사용 중인 메모리"), Bytes(static_cast<double>(info.resident_size)));
        }
    }
    rows.Group(QStringLiteral("현재 모니터"));
    AddScreenFallbackRows(rows, window);

    rows.Group(QStringLiteral("시스템"));
    rows.Add(QStringLiteral("운영체제"), QSysInfo::prettyProductName());
    {
        char brand[256]{};
        size_t size = sizeof(brand);
        if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0) {
            rows.Add(QStringLiteral("프로세서"), QString::fromUtf8(brand));
        }
    }
    rows.Add(QStringLiteral("논리 프로세서"), QString::number(QThread::idealThreadCount()) + QStringLiteral("개"));
    {
        int64_t memSize = 0;
        size_t size = sizeof(memSize);
        if (sysctlbyname("hw.memsize", &memSize, &size, nullptr, 0) == 0 && memSize > 0) {
            rows.Add(QStringLiteral("메모리"), Bytes(static_cast<double>(memSize)));
        }
    }
#else
    rows.Group(QStringLiteral("현재 모니터"));
    AddScreenFallbackRows(rows, window);
    rows.Group(QStringLiteral("시스템"));
    rows.Add(QStringLiteral("운영체제"), QSysInfo::prettyProductName());
    rows.Add(QStringLiteral("논리 프로세서"), QString::number(QThread::idealThreadCount()) + QStringLiteral("개"));
#endif
    return list;
}
