# GitHub Actions 및 Repository 설정 가이드

이 문서는 [.github/workflows/build.yml](.github/workflows/build.yml) 워크플로우를 통해 **Windows(포터블 `.zip`, 설치파일 `.exe`)**, **macOS(코드 서명된 `.dmg`)**, **Linux(포터블 `.AppImage`, `.deb` 패키지)**를 자동 빌드하고 GitHub Release로 배포하기 위해 GitHub 저장소 설정(`Settings`)에 등록해야 하는 항목을 안내합니다.

---

## 1. GitHub Actions 권한 설정 (필수)

버전 태그(`v*`) 푸시 시 자동으로 GitHub Release를 생성하고 빌드 산출물을 업로드하려면 워크플로우에 쓰기 권한이 필요합니다.

1. GitHub 저장소 페이지에서 **Settings** 탭으로 이동합니다.
2. 좌측 메뉴에서 **Actions > General**을 클릭합니다.
3. 하단의 **Workflow permissions** 섹션에서 다음을 선택합니다:
   - **`Read and write permissions`** 선택
4. **Save** 버튼을 눌러 저장합니다.

---

## 2. GitHub Secrets 설정 (`Settings > Secrets and variables > Actions`)

GitHub 저장소의 **Settings > Secrets and variables > Actions > New repository secret**에서 아래의 Secret들을 등록합니다.

> **참고:** macOS 인증서 Secret을 아직 등록하지 않은 상태에서도 빌드는 실패하지 않고 **임시 서명(Ad-hoc signing, `-`)**으로 `.dmg` 파일이 정상 생성됩니다. 인증서 Secret을 등록하면 자동으로 Apple Developer ID 정식 코드 서명(및 공증)으로 전환됩니다.

### 2.1 macOS 코드 서명(Code Signing)용 Secrets

| Secret 이름                  | 필수 여부    | 설명                                                                         | 값 예시                                               |
| ---------------------------- | ------------ | ---------------------------------------------------------------------------- | ----------------------------------------------------- |
| `MACOS_CERTIFICATE_P12`      | 서명 시 필수 | `.p12` 인증서 파일을 Base64로 인코딩한 텍스트 문자열                         | `MIIK...` (줄바꿈 없는 Base64 문자열)                 |
| `MACOS_CERTIFICATE_PASSWORD` | 서명 시 필수 | 키체인에서 `.p12` 파일을 내보낼 때 설정한 비밀번호                           | `my-p12-export-password`                              |
| `MACOS_SIGNING_IDENTITY`     | 서명 시 필수 | 서명에 사용할 인증서의 전체 이름 (Common Name)                               | `Developer ID Application: Hong Gildong (A1B2C3D4E5)` |
| `MACOS_KEYCHAIN_PASSWORD`    | 선택         | CI 러너의 임시 키체인 비밀번호 (미설정 시 매 빌드마다 무작위 UUID 자동 생성) | `temp-ci-keychain-pass`                               |

### 2.2 macOS Apple 공증(Notarization)용 Secrets (선택)

macOS에서 외부 배포용 `.dmg`를 다운로드한 사용자에게 Gatekeeper 경고 창이 뜨지 않게 하려면 코드 서명과 함께 Apple 공증(Notarization)까지 수행해야 합니다. 아래 3개 Secret을 추가로 등록하면 워크플로우가 `xcrun notarytool`과 `stapler`를 자동으로 실행합니다.

| Secret 이름                   | 필수 여부    | 설명                                                                                         | 값 예시                 |
| ----------------------------- | ------------ | -------------------------------------------------------------------------------------------- | ----------------------- |
| `APPLE_ID`                    | 공증 시 필수 | Apple Developer 계정 이메일 주소                                                             | `developer@example.com` |
| `APPLE_APP_SPECIFIC_PASSWORD` | 공증 시 필수 | [appleid.apple.com](https://appleid.apple.com)에서 생성한 **앱 암호(App-Specific Password)** | `abcd-efgh-ijkl-mnop`   |
| `APPLE_TEAM_ID`               | 공증 시 필수 | 10자리 Apple Developer Team ID                                                               | `A1B2C3D4E5`            |

---

## 3. macOS 인증서 추출 및 Secret 값 확인 방법

Mac에서 아래 순서대로 인증서를 준비하고 Secret 값을 추출할 수 있습니다.

### ① `MACOS_SIGNING_IDENTITY` 확인

Mac 터미널에서 아래 명령어를 실행하여 설치된 서명 인증서 이름을 확인합니다:

```bash
security find-identity -v -p codesigning
```

출력된 목록 중 **`Developer ID Application: 이름 (팀ID)`** 문자열(큰따옴표 안의 내용 전체)을 복사하여 `MACOS_SIGNING_IDENTITY` 값으로 등록합니다.

### ② `.p12` 파일 내보내기 (`MACOS_CERTIFICATE_PASSWORD`)

1. Mac에서 **키체인 접근(Keychain Access)** 앱을 엽니다.
2. **로그인** 키체인의 **내 인증서(My Certificates)** 항목에서 `Developer ID Application: ...` 인증서를 찾습니다.
3. 인증서 왼쪽 화살표를 펼쳐 **인증서와 개인 키(Private Key) 두 개를 함께 선택**한 뒤 우클릭 → **2개 항목 내보내기...**를 클릭합니다.
4. 파일 형식을 **개인 정보 교환(`.p12`)**으로 선택하여 `DeveloperID.p12`로 저장합니다.
5. 이때 입력한 보호용 암호를 `MACOS_CERTIFICATE_PASSWORD` Secret 값으로 등록합니다.

### ③ Base64 인코딩 (`MACOS_CERTIFICATE_P12`)

Mac 터미널에서 내보낸 `.p12` 파일을 Base64로 인코딩하여 클립보드에 복사합니다:

```bash
base64 -i DeveloperID.p12 | tr -d '\n' | pbcopy
```

(Windows PowerShell에서 인코딩할 경우):

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("DeveloperID.p12")) | Set-Clipboard
```

클립보드에 복사된 문자열을 `MACOS_CERTIFICATE_P12` Secret 값에 붙여넣습니다.

---

## 4. 빌드 및 릴리스 실행 방법

### 수동/일반 빌드 (Artifact 다운로드)

- `main` 또는 `master` 브랜치에 Push / Pull Request를 생성하거나, GitHub 저장소의 **Actions > Build and Release > Run workflow** 버튼을 클릭하면 3개 OS 빌드가 실행됩니다.
- 빌드 완료 후 워크플로우 실행 화면 하단의 **Artifacts** 섹션에서 각 OS별 빌드 결과물을 내려받을 수 있습니다:
  - `MoviePlayer-windows-x64`:
    - `MoviePlayer-<버전>-win64-portable.zip` (무설치 포터블 버전)
    - `MoviePlayer-<버전>-win64-setup.exe` (Inno Setup 설치 프로그램)
  - `MoviePlayer-macos`:
    - `MoviePlayer-<버전>-macos.dmg` (macOS 디스크 이미지)
  - `MoviePlayer-linux-x64`:
    - `MoviePlayer-<버전>-linux-x86_64.AppImage` (무설치 단일 실행 포터블 AppImage)
    - `MoviePlayer-<버전>-linux-x86_64.deb` (Ubuntu/Debian 설치 패키지)

### 정식 GitHub Release 자동 배포

`v`로 시작하는 태그를 푸시하면 3개 OS의 모든 설치/포터블 파일이 첨부된 GitHub Release가 자동으로 생성됩니다:

```bash
git tag v0.1.0
git push origin v0.1.0
```
