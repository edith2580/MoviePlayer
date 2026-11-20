#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ARCH="${ARCH:-$(uname -m)}"
VERSION="${VERSION:-$(sed -n 's/.*project(MoviePlayer VERSION \([0-9.]*\).*/\1/p' "${REPO_ROOT}/CMakeLists.txt" | head -n1)}"
VERSION="${VERSION:-0.1.1}"

WORK_ROOT="${WORK_ROOT:-${XDG_CACHE_HOME:-${HOME}/.cache}/movieplayer-appimage}"
BUILD_DIR="${WORK_ROOT}/build-${ARCH}"
APPDIR="${WORK_ROOT}/AppDir-${ARCH}"
TOOLS_DIR="${WORK_ROOT}/tools"
OUT_NAME="MoviePlayer-${VERSION}-linux-${ARCH}.AppImage"
LOCAL_OUT="${WORK_ROOT}/${OUT_NAME}"
DIST_DIR="${REPO_ROOT}/dist"

missing=()
for cmd in cmake ninja g++ pkg-config patchelf strip; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        missing+=("$cmd")
    fi
done

QMAKE_BIN="${QMAKE:-}"
if [[ -z "${QMAKE_BIN}" ]]; then
    if command -v qmake6 >/dev/null 2>&1; then
        QMAKE_BIN="$(command -v qmake6)"
    elif [[ -x /usr/lib/qt6/bin/qmake ]]; then
        QMAKE_BIN="/usr/lib/qt6/bin/qmake"
    fi
fi
if [[ -z "${QMAKE_BIN}" ]]; then
    missing+=("qt6-base-dev (qmake6)")
fi

if command -v pkg-config >/dev/null 2>&1; then
    if ! pkg-config --exists mpv; then
        missing+=("libmpv-dev")
    fi
fi

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "Missing build dependencies: ${missing[*]}" >&2
    echo >&2
    echo "Install them on Ubuntu/Debian with:" >&2
    echo "  sudo apt-get update && sudo apt-get install -y \\" >&2
    echo "    build-essential cmake ninja-build pkg-config \\" >&2
    echo "    qt6-base-dev qt6-wayland libmpv-dev libgl1-mesa-dev \\" >&2
    echo "    libxcb-cursor0 patchelf desktop-file-utils file wget" >&2
    exit 1
fi

mkdir -p "${WORK_ROOT}" "${TOOLS_DIR}" "${DIST_DIR}"

echo "==> Configuring & building MoviePlayer ${VERSION} (${ARCH})..."
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel

echo "==> Preparing AppDir..."
rm -rf "${APPDIR}"
mkdir -p \
    "${APPDIR}/usr/bin" \
    "${APPDIR}/usr/share/applications" \
    "${APPDIR}/usr/share/icons/hicolor/256x256/apps" \
    "${APPDIR}/usr/share/doc/movieplayer"

install -m 0755 "${BUILD_DIR}/MoviePlayer" "${APPDIR}/usr/bin/MoviePlayer"
strip "${APPDIR}/usr/bin/MoviePlayer"
install -m 0644 "${REPO_ROOT}/packaging/linux/movieplayer.desktop" "${APPDIR}/usr/share/applications/movieplayer.desktop"
install -m 0644 "${REPO_ROOT}/LICENSE" "${APPDIR}/usr/share/doc/movieplayer/LICENSE"
install -m 0644 "${REPO_ROOT}/res/THIRD_PARTY_NOTICES.txt" "${APPDIR}/usr/share/doc/movieplayer/THIRD_PARTY_NOTICES.txt"

ICON_DST="${APPDIR}/usr/share/icons/hicolor/256x256/apps/movieplayer.png"
python3 - "${REPO_ROOT}/res/app.ico" "${ICON_DST}" <<'PY'
import struct, sys

ico_path, png_path = sys.argv[1], sys.argv[2]
with open(ico_path, "rb") as f:
    data = f.read()

_, _, count = struct.unpack("<HHH", data[:6])
best = None
for i in range(count):
    w, h, _, _, _, _, size, offset = struct.unpack("<BBBBHHII", data[6 + i * 16 : 6 + (i + 1) * 16])
    dim = (w or 256, h or 256)
    chunk = data[offset : offset + size]
    if chunk.startswith(b"\x89PNG\r\n\x1a\n"):
        if best is None or dim > best[0]:
            best = (dim, chunk)

if best is None:
    sys.exit(1)

with open(png_path, "wb") as f:
    f.write(best[1])
PY
chmod 0644 "${ICON_DST}"

download_tool() {
    local name="$1"
    local url="$2"
    local dest="${TOOLS_DIR}/${name}"
    if [[ ! -x "${dest}" ]]; then
        echo "==> Downloading ${name}..."
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 -o "${dest}.tmp" "${url}"
        else
            wget -q -O "${dest}.tmp" "${url}"
        fi
        chmod +x "${dest}.tmp"
        mv "${dest}.tmp" "${dest}"
    fi
}

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

download_tool "linuxdeploy-${ARCH}.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage"
download_tool "linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage"

echo "==> Packaging AppImage (${OUT_NAME})..."
export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=true
export QMAKE="${QMAKE_BIN}"
export LDAI_OUTPUT="${LOCAL_OUT}"
export PATH="${TOOLS_DIR}:${PATH}"

"${LINUXDEPLOY}" \
    --appdir "${APPDIR}" \
    --executable "${APPDIR}/usr/bin/MoviePlayer" \
    --desktop-file "${APPDIR}/usr/share/applications/movieplayer.desktop" \
    --icon-file "${ICON_DST}" \
    --plugin qt \
    --output appimage

chmod +x "${LOCAL_OUT}"
cp -f "${LOCAL_OUT}" "${DIST_DIR}/${OUT_NAME}"

echo "==> Created AppImage:"
echo "    ${DIST_DIR}/${OUT_NAME}"
echo "    ${LOCAL_OUT}"
