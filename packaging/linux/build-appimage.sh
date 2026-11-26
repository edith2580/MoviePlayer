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

# Fixed releases with known hashes, the same as .github/workflows/build.yml uses.
# To upgrade, change the tags and hashes in both places together.
LINUXDEPLOY_TAG="1-alpha-20251107-1"
LINUXDEPLOY_QT_TAG="1-alpha-20250213-1"
case "${ARCH}" in
    x86_64)
        LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
        LINUXDEPLOY_QT_SHA256="15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724"
        ;;
    aarch64)
        LINUXDEPLOY_SHA256="620095110d693282b8ebeb244a95b5e911cf8f65f76c88b4b47d16ae6346fcff"
        LINUXDEPLOY_QT_SHA256="bf1c24aff6d749b5cf423afad6f15abd4440f81dec1aab95706b25f6667cdcf1"
        ;;
    *)
        echo "No pinned linuxdeploy hashes for ${ARCH}" >&2
        exit 1
        ;;
esac

# Checks the hash every run, so a cached copy from an older script (or a tampered one) is replaced.
download_tool() {
    local name="$1"
    local url="$2"
    local sha256="$3"
    local dest="${TOOLS_DIR}/${name}"
    if [[ -x "${dest}" ]] && echo "${sha256}  ${dest}" | sha256sum -c --status -; then
        return
    fi
    echo "==> Downloading ${name}..."
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 -o "${dest}.tmp" "${url}"
    else
        wget -q -O "${dest}.tmp" "${url}"
    fi
    if ! echo "${sha256}  ${dest}.tmp" | sha256sum -c --status -; then
        rm -f "${dest}.tmp"
        echo "SHA-256 mismatch for ${name}" >&2
        exit 1
    fi
    chmod +x "${dest}.tmp"
    mv "${dest}.tmp" "${dest}"
}

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage"
LINUXDEPLOY_QT="${TOOLS_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"

download_tool "linuxdeploy-${ARCH}.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/${LINUXDEPLOY_TAG}/linuxdeploy-${ARCH}.AppImage" \
    "${LINUXDEPLOY_SHA256}"
download_tool "linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${LINUXDEPLOY_QT_TAG}/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
    "${LINUXDEPLOY_QT_SHA256}"

echo "==> Packaging AppImage (${OUT_NAME})..."
export APPIMAGE_EXTRACT_AND_RUN=1
export NO_STRIP=true
export QMAKE="${QMAKE_BIN}"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"
export LDAI_OUTPUT="${LOCAL_OUT}"
export PATH="${TOOLS_DIR}:${PATH}"

QT_PLUGINS_DIR="$("${QMAKE_BIN}" -query QT_INSTALL_PLUGINS)"
DEPLOY_DEPS_ARGS=()
for dir in wayland-decoration-client wayland-graphics-integration-client wayland-shell-integration; do
    if [[ -d "${QT_PLUGINS_DIR}/${dir}" ]]; then
        mkdir -p "${APPDIR}/usr/plugins/${dir}"
        cp -a "${QT_PLUGINS_DIR}/${dir}/." "${APPDIR}/usr/plugins/${dir}/"
        DEPLOY_DEPS_ARGS+=("--deploy-deps-only=${APPDIR}/usr/plugins/${dir}")
    fi
done

"${LINUXDEPLOY}" \
    --appdir "${APPDIR}" \
    --executable "${APPDIR}/usr/bin/MoviePlayer" \
    --desktop-file "${APPDIR}/usr/share/applications/movieplayer.desktop" \
    --icon-file "${ICON_DST}" \
    --plugin qt \
    "${DEPLOY_DEPS_ARGS[@]}" \
    --output appimage

chmod +x "${LOCAL_OUT}"
cp -f "${LOCAL_OUT}" "${DIST_DIR}/${OUT_NAME}"

echo "==> Created AppImage:"
echo "    ${DIST_DIR}/${OUT_NAME}"
echo "    ${LOCAL_OUT}"
