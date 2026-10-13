# UE5-PRT-GI

[English](README.md) | **한국어** | [简体中文](README.zh-CN.md)

[사용자 가이드 · GitHub Wiki](https://github.com/mazelines/UE5-PRT-GI/wiki)

정적 장면을 위한 프로브 기반 전역 조명(GI) 플러그인입니다. Cornell Box 예제, 방향광 변화에 따른 실시간 조명 갱신, 1~4회 간접광 반사, 독립적인 프로브 볼륨과 시각화 기능을 제공합니다.

![PRT GI Cornell Box 예제](Plugins/PRTGI/docs/images/prt-gi-showcase.png)

## 설치

이 저장소는 실행용 프로젝트 `UE_PRT_GI.uproject`와 `Plugins/PRTGI`에 있는 플러그인으로 구성됩니다. `ue6` 브랜치의 프로젝트 타깃은 `Unreal6_0`을 사용합니다. 기존 플러그인의 검증 환경은 **Windows, UE 5.8, DX12 / SM6, 디퍼드 렌더링**이며, 이 기록이 현재 실행용 프로젝트의 검증을 의미하지는 않습니다.

사전 계산 캐시는 **Git LFS**로 관리합니다. 저장소를 복제한 뒤 루트 디렉터리에서 다음 명령을 실행하세요. `.surfels`와 `.multi` 파일에 LFS 포인터 텍스트 대신 실제 바이너리 데이터가 있어야 합니다.

```shell
git lfs install --local
git lfs pull
```

1. 포함된 프로젝트를 사용하려면 `UE_PRT_GI.uproject`를 엽니다. 다른 프로젝트에 설치하려면 이 저장소의 `Plugins/PRTGI` 폴더를 `YourProject/Plugins/PRTGI`로 복사합니다. 해당 폴더 바로 아래에 `PRTGI.uplugin`이 있어야 합니다.
2. 사용할 프로젝트의 UE 버전과 C++ 도구 체인에 맞춰 **Editor 타깃**을 빌드합니다. 블루프린트 프로젝트도 이 C++ 플러그인을 컴파일할 환경이 필요합니다. 필요한 경우 C++ 클래스를 추가해 프로젝트 타깃을 생성합니다.
3. **Edit → Plugins**에서 **UE5-PRT-GI**를 검색해 활성화하고, 안내에 따라 에디터를 다시 시작합니다.

GI 패스는 GBuffer를 사용하므로 현재 방식은 Forward Shading을 지원하지 않습니다. 플러그인이 Renderer 내부 인터페이스를 참조하므로 다른 엔진 버전이나 플랫폼에서는 다시 컴파일하고 검증해야 합니다. 사전 컴파일된 DLL은 엔진 빌드 간 호환을 보장하지 않습니다. 기존 구현은 설치를 위한 별도의 엔진 소스 수정을 요구하지 않습니다.

## 빠른 시작

1. 콘텐츠 브라우저에서 **Show Plugin Content**를 켭니다.
2. **PRTGI Content → Maps → PRTGI_Showcase**를 엽니다. 에셋 경로는 `/PRTGI/Maps/PRTGI_Showcase`입니다.
3. 에디터 뷰포트나 PIE 게임 화면을 클릭한 뒤 **Ctrl+Alt+2**로 PRT를 켜고, **Ctrl+Alt+3**으로 Lumen과 비교합니다.
4. **Ctrl+Alt+4**로 PRT 간접광만 확인하고, **Ctrl+Alt+P**로 프로브를 표시합니다.
5. **Ctrl+Alt+[ / ]**로 태양을 회전해 조명 변화를 확인합니다.

기존 플러그인은 UE 5.8 Renderer 인터페이스를 기준으로 작성됐습니다. 하드웨어 레이 트레이싱 Lumen과 비교하려면 프로젝트에 DX12 / SM6, Ray Tracing 및 관련 지원 설정이 필요합니다. 플러그인은 프로젝트 렌더링 설정을 강제로 덮어쓰지 않습니다.

지원 GPU에서 **Support Hardware Ray Tracing**, **Support Compute Skin Cache**, **Use Hardware Ray Tracing when available**을 활성화하세요. 재시작이 필요한 설정을 변경했다면 에디터를 다시 시작합니다. 관련 설명은 Epic의 [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine)를 참고하세요.

포함된 예제 맵의 기본 프로필은 일시적으로 `r.Lumen.HardwareRayTracing=1`, `r.Lumen.ScreenProbeGather.ScreenTraces=0`, `r.EyeAdaptation.CachedLightingPreExposure=8`을 적용하며, 맵을 벗어나면 이전 값으로 복원합니다. 따라서 예제의 Lumen 비교는 확산광 화면 추적을 끈 상태로 이루어집니다. 이 설정만으로 프로젝트에 없는 하드웨어 레이 트레이싱 지원이 활성화되지는 않습니다.

독립 검증에 사용한 설정은 [showcase-renderer-settings.ini](Plugins/PRTGI/docs/showcase-renderer-settings.ini)에 있습니다. 필요한 항목만 프로젝트에 반영하세요. 이 파일은 자동으로 적용되지 않으며, 프로젝트 설정 전체를 이 파일로 덮어쓰면 안 됩니다.

예제는 UE DirectionalLight 하나를 공유합니다. 박스의 기존 면광원 영역은 개구부로 교체했으며, 관찰을 위한 전면 개구부는 유지했습니다.

| 볼륨 | 포함된 캐시 |
|---|---|
| PRT_Global, PRT_ClassicBox, PRT_ColorBox, PRT_InvertedBox | 간접광 반사 1~4회용 데이터. 런타임 기본값은 4회 |
| PRT_ShinyBox | 면당 16×16 해상도. 검사한 프로브 878개 중 21개를 캡처 경고와 함께 제외 |
| PRT_MetallicBox | 면당 16×16 해상도, 프로브 간격 40 cm, 전송 셀 크기 2 cm. 검사한 프로브 2,212개 중 77개를 캡처 경고와 함께 제외 |

다운로드한 파일에 대용량 캐시가 없다면 조명을 비교하기 전에 **Build Precomputation**을 실행하세요.

## 장면에 프로브 볼륨 추가

1. 레벨을 저장합니다. 사전 계산에 사용하는 지오메트리와 머티리얼은 정적 상태를 유지해야 합니다.
2. UE **DirectionalLight**를 추가하고, 실시간으로 회전할 수 있도록 Mobility를 **Movable**로 설정합니다. 방향광이 여러 개라면 사용할 광원의 Actor Tag에 `PRTGI.PrimaryLight`를 추가합니다.
3. Place Actors에서 **PRT Probe Volume**을 찾아 장면에 배치합니다.
4. 볼륨 범위, 프로브 간격, Near Clip, Priority를 설정합니다.
5. PRT GI를 켠 뒤 액터의 Details → **PRT → Build**에서 **Build Precomputation**을 누릅니다.
6. 완료되면 **PRT → Status**의 **Cache Status**, **GI Active**, **Effective Bounces**, **Build Warnings**를 확인하고 레벨을 저장합니다.

볼륨은 **PRT GI 모드**에서만 간접광에 기여하고 프로브를 표시합니다. Lumen으로 전환하면 PRT GI의 기여는 꺼집니다.

### 설정 항목

| 항목 | 설명 |
|---|---|
| Enabled | 볼륨 활성화 여부 |
| Extent | 각 축 방향 반쪽 크기(cm). 액터 스케일도 범위에 반영됨 |
| Probe Spacing | 각 축의 목표 최대 간격(cm). 실제 균등 배치 간격은 Actual Spacing에서 확인 |
| Probe Inset | 볼륨 경계에서 프로브 격자를 안쪽으로 들이는 거리(cm) |
| Priority | 국소적으로 촘촘히 배치한 영역에는 더 높은 우선순위 사용 |
| Near Clip Cm | 프로브 캡처의 근거리 클리핑 거리. 기본 1 cm, 범위 0.01~10 cm. 주 카메라의 Near Clip과 별개 |
| Capture Resolution | 캡처 면당 해상도. 기본 16, 범위 4~32. 높일수록 캡처 비용 증가 |
| Transport Cell Size | 정적 표면을 묶는 단위 크기. 기본 2 cm, 범위 1~100 cm. 작을수록 대체로 데이터와 계산량 증가 |
| Blend Face / Distance | 전환에 사용할 면 하나와 거리(cm). 0=없음, 1/2=−/+X, 3/4=−/+Y, 5/6=−/+Z. 전환이 필요한 개구부에만 설정 |
| Display Mode | Hidden, Positions, Lighting. Lighting은 각 프로브의 조명 색상 표시 |
| Probe Radius | 시각화 구체의 반지름(cm). 실제 샘플링 위치에는 영향 없음 |

볼륨은 현재 **축에 정렬된 형태**이며 회전은 지원하지 않습니다. 각 축에는 최대 32개의 프로브를 배치할 수 있습니다. 한도를 넘으면 간격을 넓히거나 볼륨을 나누세요.

별도로 볼륨당 **캡처 샘플 4,000,000개** 제한이 있습니다. `ProbeCounts.X × ProbeCounts.Y × ProbeCounts.Z × 6 × CaptureResolution²`가 이 값을 넘으면 안 됩니다. 기본 해상도 16에서는 축별 제한을 지키면서 총 2,604개까지 배치할 수 있습니다. 한도를 넘으면 캡처 해상도를 낮추거나, 프로브 간격을 넓히거나, 볼륨을 나누세요.

처음에는 전역 볼륨을 Priority=0, 간격 약 200 cm로, 지역 볼륨을 Priority=10, 간격 약 30~40 cm로 설정한 뒤 장면 크기에 맞춰 조정하세요. 프로브를 벽 안에 배치하거나 막힌 벽을 가로질러 블렌딩 전환을 설정하지 마세요. Near Clip이 너무 크면 프로브 가까이의 지오메트리가 잘려 가시성 오류나 빛샘이 생길 수 있습니다.

### 사전 계산과 진행 상태

- 볼륨을 하나 이상 선택하고 **Build Precomputation**을 누르면 빌드 대기열에 추가됩니다. **Cancel Build**는 선택한 볼륨의 빌드를 취소합니다.
- **PRT → Build**의 **Build Progress**는 지오메트리 캡처, 전송 계산, 저장을 포함한 진행률입니다. **Build Stage**에는 현재 작업이 표시됩니다. 유효한 지오메트리 캐시가 있으면 재사용하므로 전송 단계만 실행될 수도 있습니다. 진행률은 단계별 가중치이며 남은 시간의 추정값이 아닙니다.
- 빌드는 UE 장면 캡처와 CPU 전송 사전 계산을 함께 사용합니다. 런타임 조명 갱신은 기본적으로 GPU에서 처리하지만 빌드 전체가 GPU 베이크인 것은 아닙니다.
- 빌드 시 간접광 반사 1~4회 전환에 필요한 데이터를 저장합니다. 캐시가 있는 반사 횟수로 전환할 때는 장면을 다시 캡처하지 않습니다. 기본값은 4회입니다.
- 빌드 시간은 프로브 수, 캡처 해상도, 지오메트리 복잡도, 전송 셀 크기와 하드웨어에 따라 달라집니다. 작은 볼륨에서 먼저 검증한 뒤 범위를 넓히세요.
- 정적 지오메트리, 머티리얼, 프로브 격자 배치, 캡처 매개변수를 바꾸면 다시 빌드해야 합니다. 볼륨을 복제하면 새 GUID가 생성되므로 복제본에도 별도의 캐시가 필요합니다.

지오메트리 검사 결과가 맞지 않거나 캡처가 비어 있는 프로브는 전송과 보간에서 제외됩니다. 제외된 수가 검사한 프로브의 5% 이하이고 나머지 데이터가 검증을 통과하면 경고와 함께 빌드가 완료될 수 있습니다. 이를 넘는 오류는 빌드를 중단시킵니다. 결과를 사용하기 전에 **Build Warnings**, **Invalid Probes**와 해당 영역을 확인하세요. 캡처 검사는 **Visibility** 채널의 충돌 쿼리를 사용하므로, 참여 지오메트리에는 렌더링 표면과 일치하며 이 채널을 차단하는 충돌 설정이 필요합니다.

볼륨당 전송 항목은 **1,600만 개**, 항목당 64바이트로 제한되며 전송 데이터는 최대 약 **976.6 MiB**입니다. 파일 로드 한도에는 지오메트리 인덱스 등을 위한 128 MiB가 추가로 허용됩니다. 빌드 임시 복사본, CPU 캐시, GPU 버퍼는 별도 메모리를 사용하므로 이 수치는 총 메모리 한도가 아닙니다. 한도에 가까워져도 캡처 해상도가 자동으로 낮아지지는 않습니다.

## 콘텐츠 구조

아래 경로는 플러그인 폴더 `Plugins/PRTGI`를 기준으로 합니다.

- `Content/Maps`: 예제 레벨.
- `Content/Showcase/Materials`, `Meshes` 등: 예제에 필요한 프로젝트 에셋의 복사본. 참조는 플러그인 내부 에셋으로 연결됩니다.
- `Content/Materials`: 프로브 시각화와 지오메트리 캡처용 머티리얼.
- `Content/Precomputed/Volumes`: 레벨 패키지 이름과 볼륨 GUID별로 구분한 `.surfels` / `.multi` 원시 캐시. 광원 매개변수는 포함하지 않습니다.

`.surfels`는 캡처한 지오메트리를, 짝이 되는 `.multi`는 다중 반사 전송 데이터를 저장합니다. 두 파일을 함께 보관하세요. 원시 바이너리 파일이므로 콘텐츠 브라우저에 `.uasset`으로 표시되지 않습니다. 캐시는 고정된 태양 각도의 최종 이미지가 아닙니다. 런타임에도 현재 광원 입력과 가림을 계산한 뒤 사전 계산된 전송 데이터를 적용합니다.

`/PRTGI/` 아래의 플러그인 레벨은 플러그인 내부의 `Content/Precomputed/Volumes/<레벨-패키지>/<GUID>.prtgi.surfels`와 `<GUID>.prtgi.surfels.multi`에 저장합니다. `/Game/`을 포함한 다른 레벨은 `YourProject/Content/PRTGI/Volumes/<레벨-패키지>/`에 저장합니다. 레벨 패키지 이름의 슬래시는 밑줄로 바뀝니다. 레벨을 이동하거나 이름을 바꾸면 캐시 경로도 바뀌므로 **Cache File**을 확인하고 필요하면 다시 빌드하세요.

플러그인의 빌드 규칙은 자체 `Content/Precomputed` 폴더를 별도의 **NonUFS** 파일로 패키징합니다. `/Game/` 레벨을 사용하는 프로젝트에서는 **Additional Non-Asset Directories To Copy**에 `PRTGI/Volumes`를 추가해 프로젝트의 `Content/PRTGI/Volumes`도 포함해야 합니다. 입력 경로는 프로젝트 Content 기준입니다. 기존 검증 기록은 에디터 사용을 대상으로 하므로 패키징 결과에서도 캐시 로드를 확인하세요.

## 현재 제한 사항

- PRT는 선택한 DirectionalLight 하나에 의한 간접 확산광을 계산합니다. Point, Spot, Rect Light, SkyLight, 발광 머티리얼은 이 전송 계산의 광원으로 사용되지 않습니다. 플러그인은 스페큘러 GI나 반사를 제공하지 않습니다.
- 전송에 사용하는 지오메트리와 머티리얼은 정적 상태여야 합니다. 런타임 광원이 바뀌면 GPU 가림 계산에 캐시된 지오메트리를 재사용합니다. 움직이는 차폐물은 캐시된 전송에 자동으로 반영되지 않습니다.
- 곡면에 프로브로 인한 고리 무늬가 나타나거나 Lumen과 밝기가 다를 수 있습니다. 조명을 비교할 때 실제 장면의 결과를 확인하세요.

## 단축키

에디터 또는 PIE 뷰포트를 클릭해 포커스를 준 뒤 사용하세요.

| 단축키 | 동작 |
|---|---|
| Ctrl+Alt+1 | 동적 GI 끄기. 이 예제에는 오프라인 베이크 조명이 없음 |
| Ctrl+Alt+2 | PRT GI |
| Ctrl+Alt+3 | Lumen GI |
| Ctrl+Alt+4 | PRT 간접 확산광만 보기 / 전체 화면 전환 |
| Ctrl+Alt+P | 프로브 표시 / 숨기기. 위치 마커 또는 조명 구체 표시는 볼륨의 Display Mode에 따름 |
| Ctrl+Alt+G | 간접광 반사 1~4회 순환. 해당 캐시 필요 |
| Ctrl+Alt+[ / ] | DirectionalLight Yaw를 기본 간격인 5°만큼 감소 / 증가 |
| Ctrl+Alt+Shift+[ / ] | DirectionalLight Pitch를 기본 간격인 5°만큼 감소 / 증가 |
| Ctrl+Alt+B | 이전 방식의 전역 베이크 명령. 액터 볼륨은 빌드하지 않음 |

지역 사전 계산은 해당 `PRTProbeVolume`을 선택하고 **Build Precomputation**을 눌러 실행합니다. 간접광 반사 기본값은 4회입니다.

포커스나 키보드 배열 때문에 단축키가 작동하지 않으면 UE 콘솔에서 다음 명령을 하나씩 실행하세요.

```text
PRTGI.SetMode PRT
PRTGI.SetMode Lumen
PRTGI.ToggleIndirectOnly
r.PRTGI.ShowProbes 1
r.PRTGI.Bounces 4
PRTGI.RotateLight 5 0
PRTGI.Status
```

`PRTGI.SetMode Offline`은 동적 GI를 종료하는 이전 명령 이름입니다. 오프라인 조명이나 Path Tracing 기준 이미지를 자동으로 생성하지는 않습니다.
