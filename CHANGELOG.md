# 변경 내역 (Changelog)

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
