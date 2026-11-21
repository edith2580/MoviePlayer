#pragma once

#include <QtGlobal>

#include <array>

namespace AppInfo {

inline constexpr const char* kAppName = "Movie Player";
inline constexpr const char* kGithubOwner = "edith2580";
inline constexpr const char* kGithubRepo = "MoviePlayer";
inline constexpr const char* kGithubUrl = "https://github.com/edith2580/MoviePlayer";
inline constexpr const char* kGithubReleasesUrl = "https://github.com/edith2580/MoviePlayer/releases";
inline constexpr const char* kGithubLatestReleaseApiUrl =
    "https://api.github.com/repos/edith2580/MoviePlayer/releases/latest";

// Inno Setup AppId (packaging/windows/installer.iss) used to detect installed vs portable builds on Windows.
inline constexpr const char* kWindowsInnoAppId = "{8E9A74B4-6F8C-4D25-9B27-55F267A9C102}";

struct LibraryInfo {
    const char* name;
    const char* version;
    const char* license;
    const char* description;
    const char* url;
};

// 설정 > 정보 탭에 표시되는 사용 라이브러리 목록입니다.
// 아래 목록에 항목을 수동으로 추가하거나 삭제하면 정보 탭에 바로 반영됩니다.
inline constexpr auto kUsedLibraries = std::to_array<LibraryInfo>({
    {
        "Qt 6",
        QT_VERSION_STR,
        "LGPL-3.0 / GPL-3.0",
        "크로스 플랫폼 UI, OpenGL 위젯 및 네트워크 프레임워크",
        "https://www.qt.io",
    },
    {
        "libmpv (mpv)",
        "",
        "GPL-2.0+ / LGPL-2.1+",
        "동영상/오디오 재생 엔진",
        "https://mpv.io",
    },
    {
        "FFmpeg",
        "",
        "GPL-3.0 / LGPL-2.1+",
        "멀티미디어 코덱 및 포맷 처리 (libmpv 내장)",
        "https://ffmpeg.org",
    },
    {
        "libass",
        "",
        "ISC",
        "ASS/SSA 자막 렌더링 엔진 (libmpv 내장)",
        "https://github.com/libass/libass",
    },
});

}  // namespace AppInfo
