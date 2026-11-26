# 변경 내역 (Changelog)

## v0.1.5

### 보안 수정
- **임시 폴더를 본인 전용으로 변경** (Linux 다중 사용자 환경)
  - 언어별로 나눈 SMI 자막과 업데이트 다운로드를 고정 이름의 `/tmp/MoviePlayer…` 대신, 이름이 무작위이고 본인만 접근할 수 있는(0700) 임시 폴더에 저장합니다.
  - 같은 PC의 다른 사용자가 폴더를 미리 만들거나 심볼릭 링크를 심어, 영상을 열 때 파일이 지워지거나 업데이트 파일이 바꿔치기될 수 있던 문제를 막았습니다.
- **업데이트 파일 SHA-256 검증**
  - 내려받은 파일을 GitHub 릴리스에 등록된 SHA-256과 비교해, 일치할 때만 설치하거나 교체합니다. 일치하지 않으면 파일을 지우고 업데이트를 중단합니다.
  - 해시 정보가 없는 릴리스는 자동 업데이트 대신 릴리스 페이지를 엽니다.
- **단일 인스턴스 IPC를 현재 사용자로 제한**
  - IPC 이름을 사용자마다 다르게 만들고 본인만 접속할 수 있게 했습니다. 실행 중인 창에 파일을 넘기기 전에 그 창이 같은 사용자의 것인지 확인합니다.
  - 다른 사용자가 이름을 먼저 차지해 앱이 바로 종료되거나 열려던 파일 경로가 새어 나가던 문제를 막았습니다.
- **조작된 SMI 자막으로 앱이 멈추는 문제 수정**
  - SMI를 언어별로 나눌 때 언어는 8개, 결과 크기는 128MB까지만 처리하고, 넘으면 원본 파일을 그대로 씁니다.
  - 처리 시간이 입력 크기의 제곱으로 늘어나던 부분을 고쳤습니다.
- **macOS hardened runtime 예외 제거**
  - 필요 없던 JIT, 서명 없는 실행 메모리, 라이브러리 검증 해제 entitlements를 모두 뺐습니다.

### 빌드 / 배포
- GitHub Actions 빌드 작업의 권한을 읽기 전용으로 낮추고, 릴리스 작업에만 쓰기 권한을 줬습니다.
- 외부 액션을 커밋 SHA로 고정하고, 태그 이름은 스크립트에 바로 넣지 않고 환경 변수로 읽습니다.
- linuxdeploy와 linuxdeploy-plugin-qt를 `continuous` 대신 정식 릴리스와 SHA-256으로 고정했습니다. (`packaging/linux/build-appimage.sh`도 같음)

---

## v0.1.4

### Linux AppImage 호환성 개선
- **Linux AppImage (`x86_64` / `arm64`) Qt 6.8.2 업그레이드**
  - 기존 Ubuntu 24.04 기본 Qt 6.4.2에서 발생하던 KDE Plasma 6(Qt 6.7+) `QFont::fromString` 경고 및 Wayland 화면 갱신 문제를 해결했습니다.
  - Windows 빌드와 동일한 Qt 6.8.2(`qtwayland` 포함)로 통일했습니다. (`.deb` 패키지는 기존처럼 시스템 Qt6 의존성을 유지합니다.)
- **AppImage 필수 Qt 플러그인 자동 감지 및 번들링 강화**
  - Wayland 플랫폼 플러그인(`libqwayland-egl.so`, `libqwayland-generic.so`, `libqwayland.so`)을 Qt 버전에 맞춰 자동 탐지하도록 개선했습니다.
  - `xcbglintegrations`(X11 OpenGL) 및 `tls`(GitHub 자동 업데이트 HTTPS) 플러그인 번들링 및 검증을 추가했습니다.
- **문서 보강**: `README.md`에 Linux AppImage 실행 방법 및 Ubuntu / Debian / CachyOS / Arch Linux (최소 설치·WSL) 필수 패키지 안내를 추가했습니다.

---

## v0.1.3

### 새 기능
- **설정 > 정보 탭 추가**
  - 앱 아이콘, 프로그램 이름, 현재 버전, 실행 형태(Windows 설치/포터블, macOS 앱 번들, Linux AppImage/.deb) 표시
  - **GitHub 열기**, **릴리스 페이지** 바로가기 버튼 추가
- **GitHub Release 기반 자동 업데이트**
  - 최신 릴리스 버전 비교 및 다운로드 진행률 표시
  - **Windows 설치 버전**: Inno Setup Uninstall 레지스트리를 확인해 설치 버전을 판별하고, 최신 인스톨러(`-win64-setup.exe`)를 내려받아 자동 업데이트 및 재시작
  - **Windows 포터블 버전**: 최신 포터블(`.zip`)을 내려받아 파일 교체 후 자동 재시작
  - **macOS / Linux**: `.dmg`(macOS) 및 `.AppImage`(Linux) 자동 업데이트 지원, `.deb` 패키지는 다운로드 및 설치 안내 제공
- **사용 라이브러리 정보 표시**
  - 설정 > 정보 탭에 사용 중인 오픈소스 라이브러리 목록(라이브러리, 라이선스, 설명) 표시 및 항목 더블클릭 시 프로젝트 페이지 열기
  - `src/AppInfo.h`의 `AppInfo::kUsedLibraries` 변수에서 항목을 간편하게 추가/삭제 가능

### 버그 수정
- **동영상 재생 중 창 닫기(`X`) 시 앱이 멈추는 현상 수정**
  - 종료 시 재생 정지 및 `mpv` 콜백 선제 해제 후 OpenGL 렌더 컨텍스트를 안전하게 정리하도록 개선
  - 창을 즉시 숨기고 `mpv_terminate_destroy()`에 타임아웃 보호를 적용해 디코더/오디오/네트워크 지연 시에도 창이 응답 없음 상태로 멈추지 않도록 수정
  - 백그라운드 시리즈 탐색 스레드 종료 안전성 개선(`QPointer` 가드)

### UI / 기타 개선
- 설정 창의 모든 버튼에 네이티브 스타일을 유지한 여백과 최소 크기 적용 및 정보 탭 레이아웃 간격 개선
- `CMakeLists.txt`의 `PROJECT_VERSION`이 앱 버전에 자동 동기화되도록 개선

---

## v0.1.2

- Windows 전체화면을 독점 모드 대신 꽉 찬 테두리 없는 창(Borderless Fullscreen)으로 변경
- 전체화면 동영상 재생 중 모니터 절전 모드 진입 방지 (Windows / macOS / Linux)
- 자막 파일 드래그 앤 드롭 시 연결된 동영상 자동 재생 지원
- 전 플랫폼 `QOpenGLWidget` + `mpv_render_context` 렌더링 통합 및 화면 노이즈 해결
