# 동영상 플레이어

Qt6 + [libmpv](https://mpv.io/manual/master/#embedding-into-other-programs-libmpv) 기반의 멀티플랫폼(Windows, macOS, Linux) 동영상 플레이어입니다.

- **원본 프로젝트 (Win32 / DirectX):** https://github.com/iuscaler/movie-player

## 빌드

C++20 컴파일러, CMake 3.25 이상, **Qt 6** (`Core`, `Gui`, `Widgets`, `OpenGLWidgets`, `Network`)가 필요합니다.

### Windows
Visual Studio 2026의 "C++를 사용한 데스크톱 개발" 워크로드와 Qt6(MSVC x64)가 필요합니다. `libmpv`는 처음 구성할 때 `third_party/`에 자동으로 받습니다.

cmake 찾이 못할때

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:PATH"
```

빌드
```powershell
cmake --preset vs2026-x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.0/msvc2022_64"
cmake --build --preset release
```

새 배포용 zip 만들기: `cmake --build --preset release --target PACKAGE`


### Linux Package Install

```bash
sudo apt-get update && sudo apt-get install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-wayland libmpv-dev libgl1-mesa-dev \
  libxcb-cursor0 patchelf desktop-file-utils file wget
```

### Linux / macOS
시스템 패키지 관리자 또는 Homebrew로 `qt6`, `mpv`(`libmpv`), `pkg-config`, `ninja`를 설치한 뒤 빌드합니다.

```bash
cmake --preset ninja-release
cmake --build --preset ninja-release
```

### GitHub Actions 자동 빌드 및 릴리스
Windows(포터블 `.zip` 및 설치파일 `.exe`), macOS(코드 서명 `.dmg`), Linux(포터블 `.AppImage` 및 설치파일 `.deb`) 자동 빌드 워크플로우가 [.github/workflows/build.yml](.github/workflows/build.yml)에 구성되어 있습니다.
macOS 코드 서명 인증서 Secret 및 GitHub 저장소 권한 설정 방법은 [GITHUB_SETTINGS.md](GITHUB_SETTINGS.md)를 참고하세요.

## 실행

```powershell
build\Release\MoviePlayer.exe "동영상 경로"
```

## 구조

| 파일 | 역할 |
|---|---|
| `src/main.cpp` | 시작할 파일 결정, 단일 인스턴스 IPC(`QLocalServer`/`QLocalSocket`), 이벤트 루프 |
| `src/MainWindow.*` | 메인 창과 입력 처리, 비디오 영역과 컨트롤 바 및 하이브리드 재생 목록 레이아웃 제어 |
| `src/MpvVideoWidget.*` | `QOpenGLWidget` + `mpv_render_context` 기반 비디오 프레임버퍼 렌더링 |
| `src/ControlBar.*` | 영상 위에 합성되는 반투명 재생 바 (`QPainter` + `QGraphicsOpacityEffect`) |
| `src/PlaylistPanel.*` | 하이브리드 재생 목록 패널 (창 모드: 우측 분할 확장 / 전체화면: 반투명 다크 오버레이) |
| `src/ContextMenu.*` | 우클릭 메뉴 및 챕터/재생 목록 팝업 메뉴 (`QMenu`) |
| `src/SettingsDialog.*` | 설정 창 (`QDialog` + `QTabWidget`) |
| `src/InfoDialog.*` | 실시간 재생 정보 창 (`QDialog` + `QTreeWidget` + `QTimer`) |
| `src/PlaybackInfo.*` | 재생, 파일, 시스템(OS별 하드웨어/모니터) 정보 수집 |
| `src/Shortcuts.*` | 단축키 목록과 기본값, 키 이름 (`QKeyCombination`) |
| `src/FileAssociation.*` | 연결 프로그램 등록과 해제 (Windows 레지스트리 / Linux `.desktop` / macOS) |
| `src/FileDialogs.*` | 파일 선택 창(`QFileDialog`), 파일 종류 판별 및 자막-영상 매칭 |
| `src/Settings.*` | 설정 저장과 불러오기 (`QSettings` INI 형식) |
| `src/Playlist.*` | 자연 정렬(`QCollator`) 및 같은 폴더 내 시리즈 자동 탐색 |
| `src/SmiSubtitles.*` | 여러 언어가 든 SMI 파일을 언어별로 분리 |
| `src/MpvPlayer.*` | libmpv 호출 및 비동기 이벤트/속성 관찰 시그널 담당 |

## 조작

아래 키는 기본값이며, 우클릭 메뉴의 "설정 > 단축키"에서 바꿀 수 있습니다.

| 입력 | 동작 |
|---|---|
| `Space` | 재생/일시정지 (영상이 끝난 뒤에는 처음부터 재생) |
| `←` / `→` | 뒤로/앞으로 이동 (기본 5초) |
| `↑` / `↓`, 마우스 휠 | 볼륨 조절 |
| `Enter`, 더블클릭 | 전체화면 전환 |
| `Esc` | 재생 목록 닫기 / 전체화면 해제 |
| 영상 드래그 | 창 이동 (제목 표시줄을 끄는 것과 같음) |
| 파일 끌어다 놓기 | 해당 파일 재생 |
| 재생 바 | 재생/일시정지, 탐색, 볼륨, 전체화면 (슬라이더는 클릭 또는 드래그) |
| 우클릭 | 파일 열기, 자막/오디오, 재생, 영상, 설정 메뉴 |
| `S` / `A` | 자막/오디오 트랙 전환 |
| `,` / `.` / `/` | 자막 싱크 0.1초 빠르게/느리게/초기화 |
| 자막 파일 끌어다 놓기 | 현재 영상에 자막 추가 |
| `C` / `X` / `Z` | 배속 0.1 빠르게/느리게/원래대로 (0.25~4배) |
| `J` / `K` / `L` | 구간 반복 시작 지정 / 끝 지정 / 해제 |
| `F` / `D` | 다음/이전 프레임 |
| `Q` `W` / `E` `R` / `T` `Y` / `U` `I` / `O` `P` | 밝기 / 대비 / 채도 / 색조 / 감마 낮추기, 높이기 |
| `Backspace` | 영상 조정 초기화 |
| `[` / `]` / `\` | 음성 싱크 0.1초 빠르게/느리게/초기화 |
| `H` | 챕터 목록 (선택하면 그 챕터로 이동) |
| `F1` | 재생 정보 (재생, 파일, 시스템) |
| `F5` | 설정 |
| `F6` | 재생 목록 (자동 재생, 한 개 반복, 전체 목록 반복도 여기서 켜고 끔) |
| `PageDown` / `PageUp` | 다음 / 이전 파일 |
| `1` / `2` / `3` / `4` | 창을 영상 원본의 50% / 100% / 150% / 200% 크기로 (모니터를 넘지 않는 만큼만) |

화면 비율과 회전은 우클릭 메뉴의 "영상"에서 바꿉니다. 챕터가 있는 영상은 탐색 막대에 챕터 경계가 노란 삼각형으로 표시됩니다.

새 파일을 열면 싱크, 구간 반복, 회전, 화면 비율은 초기화되고, 배속은 앱을 끌 때까지, 영상 조정은 다음 실행까지 유지됩니다.

영상을 열면 창이 영상의 원본 크기에 맞춰집니다. 모니터보다 큰 영상은 비율을 유지한 채 작업 영역에 맞춰지며, 최대화나 전체화면 중에는 창 크기를 바꾸지 않습니다.

영상과 이름이 같거나 이름을 포함하는 자막 파일은 자동으로 불러옵니다. 유니코드가 아닌 자막은 CP949로 읽으며, 다른 인코딩은 메뉴의 "자막 인코딩 > 자동 감지"로 읽을 수 있습니다. 한 SMI 파일에 여러 언어가 있으면 언어별 트랙으로 나누고 한국어를 먼저 선택합니다.

## 재생 목록

- 영상 하나를 열면 같은 폴더에서 이름이 숫자만 다른 파일(다른 회차)을 찾아 재생 목록을 만듭니다. `[27AB50FF]` 같은 대괄호 안의 8자리 체크섬은 무시합니다. 순서는 자연 정렬 이름순과 같아 2화가 10화보다 앞에 옵니다.
- 파일 열기에서 여러 개를 고르거나, 여러 파일을 창에 끌어다 놓거나, 탐색기에서 여러 개를 선택해 열면 그 파일들이 재생 목록이 됩니다.
- `F6`으로 재생 목록 패널을 열면 일반 창 모드에서는 영상 영역 크기를 유지한 채 메인 창 우측이 확장되어 나란히 배치되며, 전체화면 모드에서는 영상 우측 위에 반투명 다크 오버레이 서랍으로 표시됩니다.
- 영상이 끝나면 다음 파일을 재생합니다. 다음 파일로 넘어갈 때 창 크기는 유지되며, 설정의 일반 탭에서 영상 크기에 맞추도록 바꿀 수 있습니다.
- 재생 목록은 저장하지 않으며 앱을 끄면 사라집니다. 자동 재생과 반복 설정은 유지됩니다.

## 문제 해결

환경 변수 `MOVIE_PLAYER_MPV_LOG`에 파일 경로를 지정하면 mpv 로그를 남깁니다. 하드웨어 디코딩 사용 여부나 선택된 자막 트랙을 확인할 때 유용합니다.

## 라이선스

이 프로그램은 [GNU GPL 버전 3](LICENSE) 또는 그 이후 버전으로 배포되는 자유 소프트웨어이며, 어떠한 보증도 제공하지 않습니다. 함께 배포되는 libmpv의 출처와 소스 코드 위치는 [THIRD_PARTY_NOTICES.txt](res/THIRD_PARTY_NOTICES.txt)에 있습니다.
